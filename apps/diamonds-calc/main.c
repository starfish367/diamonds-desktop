/* Diamonds Calculator - Standard & Scientific GTK3 calculator for diamonds-desktop.
 * Pure C11 + GTK3 + libm, recursive-descent expression parser (depth-limited, no eval).
 */
#include "diamonds_gtk.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

#define MAX_EXPR_LEN    512
#define MAX_PARSE_DEPTH 64
#define MAX_HISTORY     50

typedef enum {
	PARSE_OK = 0,
	PARSE_ERR_SYNTAX,
	PARSE_ERR_DIV_ZERO,
	PARSE_ERR_DOMAIN,
	PARSE_ERR_OVERFLOW,
	PARSE_ERR_DEPTH
} ParseStatus;

typedef struct {
	const char *src;
	size_t pos;
	int depth;
	bool deg_mode;
	ParseStatus err;
} Parser;

typedef struct {
	char expr[MAX_EXPR_LEN];
	char result[64];
	double value;
} HistoryItem;

typedef struct {
	GtkWidget *window;
	GtkWidget *expr_label;
	GtkWidget *result_label;
	GtkWidget *sci_revealer;
	GtkWidget *deg_rad_btn;
	GtkWidget *mode_btn;
	GtkWidget *history_list;

	char expr[MAX_EXPR_LEN];
	char current_result[64];
	double last_value;
	bool has_last_value;
	bool just_evaluated;
	bool has_error;
	bool scientific_mode;
	bool deg_mode;

	HistoryItem history[MAX_HISTORY];
	size_t history_count;
} CalcApp;

static const char *CALC_EXTRA_CSS =
	".calc-display { background-color: #1a1b26; border: 1px solid #282a36; border-radius: 10px; padding: 14px 18px; }"
	".calc-expr { font-size: 14px; color: #94a3b8; min-height: 20px; }"
	".calc-result { font-size: 36px; font-weight: 700; color: #f8fafc; font-family: Segoe UI, Cantarell, sans-serif; }"
	"button.calc-btn { background-color: #20202a; color: #e2e8f0; border: 1px solid #2e303e; border-radius: 8px;"
	"  font-size: 16px; font-weight: 600; min-height: 46px; min-width: 56px; }"
	"button.calc-btn:hover { background-color: #2a2b38; }"
	"button.calc-btn:active { background-color: #323444; }"
	"button.calc-num { background-color: #252634; color: #ffffff; font-size: 18px; font-weight: 600; }"
	"button.calc-num:hover { background-color: #2f3142; }"
	"button.calc-sci { background-color: #1c1d28; color: #cbd5e1; font-size: 14px; font-weight: 600; min-height: 40px; }"
	"button.calc-sci:hover { background-color: #262837; color: #ffffff; }"
	"button.calc-eq { background-color: #0284c7; color: #ffffff; border: none; border-radius: 8px;"
	"  font-size: 20px; font-weight: 700; min-height: 46px; }"
	"button.calc-eq:hover { background-color: #0369a1; }"
	"button.calc-eq:active { background-color: #075985; }"
	".history-panel { background-color: #1a1b26; border: 1px solid #282a36; border-radius: 10px; padding: 8px; }"
	".history-row { padding: 6px 8px; border-radius: 6px; }"
	".history-expr { font-size: 12px; color: #94a3b8; }"
	".history-val { font-size: 16px; font-weight: 700; color: #f8fafc; }";

/* ---------------- Recursive-Descent Expression Parser ---------------- */
static double parse_expr(Parser *p);

static void skip_spaces(Parser *p) {
	while (p->src[p->pos] != '\0' && isspace((unsigned char)p->src[p->pos])) {
		p->pos++;
	}
}

static bool match_str(Parser *p, const char *lit) {
	skip_spaces(p);
	size_t len = strlen(lit);
	if (strncmp(p->src + p->pos, lit, len) == 0) {
		p->pos += len;
		return true;
	}
	return false;
}

static bool match_ident(Parser *p, const char *ident) {
	skip_spaces(p);
	size_t len = strlen(ident);
	if (strncmp(p->src + p->pos, ident, len) == 0) {
		char next = p->src[p->pos + len];
		if (!isalnum((unsigned char)next) && next != '_') {
			p->pos += len;
			return true;
		}
	}
	return false;
}

static double calc_factorial(Parser *p, double v) {
	if (v < 0.0 || floor(v) != v) {
		p->err = PARSE_ERR_DOMAIN;
		return 0.0;
	}
	if (v > 170.0) {
		p->err = PARSE_ERR_OVERFLOW;
		return 0.0;
	}
	double res = 1.0;
	int n = (int)v;
	for (int i = 2; i <= n; i++) {
		res *= (double)i;
	}
	return res;
}

static double apply_func(Parser *p, const char *fn, double arg) {
	if (p->err != PARSE_OK) return 0.0;

	if (strcmp(fn, "sin") == 0) {
		double rad = p->deg_mode ? (arg * M_PI / 180.0) : arg;
		/* Snap exact multiples of 180 deg to 0 */
		if (p->deg_mode && fmod(fabs(arg), 180.0) == 0.0) return 0.0;
		return sin(rad);
	}
	if (strcmp(fn, "cos") == 0) {
		double rad = p->deg_mode ? (arg * M_PI / 180.0) : arg;
		if (p->deg_mode && fmod(fabs(arg - 90.0), 180.0) == 0.0) return 0.0;
		return cos(rad);
	}
	if (strcmp(fn, "tan") == 0) {
		if (p->deg_mode && fmod(fabs(arg - 90.0), 180.0) == 0.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		double rad = p->deg_mode ? (arg * M_PI / 180.0) : arg;
		return tan(rad);
	}
	if (strcmp(fn, "asin") == 0) {
		if (arg < -1.0 || arg > 1.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		double r = asin(arg);
		return p->deg_mode ? (r * 180.0 / M_PI) : r;
	}
	if (strcmp(fn, "acos") == 0) {
		if (arg < -1.0 || arg > 1.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		double r = acos(arg);
		return p->deg_mode ? (r * 180.0 / M_PI) : r;
	}
	if (strcmp(fn, "atan") == 0) {
		double r = atan(arg);
		return p->deg_mode ? (r * 180.0 / M_PI) : r;
	}
	if (strcmp(fn, "ln") == 0) {
		if (arg <= 0.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		return log(arg);
	}
	if (strcmp(fn, "log") == 0) {
		if (arg <= 0.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		return log10(arg);
	}
	if (strcmp(fn, "sqrt") == 0) {
		if (arg < 0.0) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		return sqrt(arg);
	}
	if (strcmp(fn, "sqr") == 0) {
		return arg * arg;
	}
	if (strcmp(fn, "inv") == 0) {
		if (arg == 0.0) {
			p->err = PARSE_ERR_DIV_ZERO;
			return 0.0;
		}
		return 1.0 / arg;
	}
	p->err = PARSE_ERR_SYNTAX;
	return 0.0;
}

static double parse_primary(Parser *p) {
	if (p->err != PARSE_OK) return 0.0;
	if (++p->depth > MAX_PARSE_DEPTH) {
		p->err = PARSE_ERR_DEPTH;
		return 0.0;
	}

	skip_spaces(p);
	double val = 0.0;

	if (match_str(p, "(")) {
		val = parse_expr(p);
		if (!match_str(p, ")")) {
			if (p->err == PARSE_OK) p->err = PARSE_ERR_SYNTAX;
		}
		p->depth--;
		return val;
	}

	/* Constants: π, pi, e */
	if (match_str(p, "π") || match_ident(p, "pi")) {
		p->depth--;
		return M_PI;
	}
	if (match_ident(p, "e")) {
		p->depth--;
		return M_E;
	}

	/* Prefix √ */
	if (match_str(p, "√")) {
		double sub = parse_primary(p);
		val = apply_func(p, "sqrt", sub);
		p->depth--;
		return val;
	}

	/* Named functions */
	static const char *funcs[] = {
		"asin", "acos", "atan", "sin", "cos", "tan",
		"sqrt", "sqr", "inv", "ln", "log", NULL
	};
	for (int i = 0; funcs[i] != NULL; i++) {
		if (match_ident(p, funcs[i])) {
			double arg = 0.0;
			if (match_str(p, "(")) {
				arg = parse_expr(p);
				if (!match_str(p, ")")) {
					if (p->err == PARSE_OK) p->err = PARSE_ERR_SYNTAX;
				}
			} else {
				arg = parse_primary(p);
			}
			val = apply_func(p, funcs[i], arg);
			p->depth--;
			return val;
		}
	}

	/* Number literal */
	skip_spaces(p);
	const char *start = p->src + p->pos;
	if (isdigit((unsigned char)*start) || *start == '.') {
		char *endptr = NULL;
		errno = 0;
		val = strtod(start, &endptr);
		if (endptr == start) {
			p->err = PARSE_ERR_SYNTAX;
			p->depth--;
			return 0.0;
		}
		if (errno == ERANGE || !isfinite(val)) {
			p->err = PARSE_ERR_OVERFLOW;
			p->depth--;
			return 0.0;
		}
		p->pos += (size_t)(endptr - start);
		p->depth--;
		return val;
	}

	p->err = PARSE_ERR_SYNTAX;
	p->depth--;
	return 0.0;
}

static double parse_postfix(Parser *p) {
	double val = parse_primary(p);
	while (p->err == PARSE_OK) {
		skip_spaces(p);
		if (match_str(p, "!")) {
			val = calc_factorial(p, val);
		} else if (match_str(p, "%")) {
			val = val / 100.0;
		} else if (match_str(p, "²")) {
			val = val * val;
		} else {
			break;
		}
	}
	return val;
}

static double parse_unary(Parser *p) {
	if (p->err != PARSE_OK) return 0.0;
	if (++p->depth > MAX_PARSE_DEPTH) {
		p->err = PARSE_ERR_DEPTH;
		return 0.0;
	}

	double val;
	if (match_str(p, "+")) {
		val = parse_unary(p);
	} else if (match_str(p, "-") || match_str(p, "−")) {
		val = -parse_unary(p);
	} else {
		val = parse_postfix(p);
	}
	p->depth--;
	return val;
}

static double parse_power(Parser *p) {
	double left = parse_unary(p);
	if (p->err != PARSE_OK) return 0.0;

	if (match_str(p, "^")) {
		if (++p->depth > MAX_PARSE_DEPTH) {
			p->err = PARSE_ERR_DEPTH;
			return 0.0;
		}
		double right = parse_power(p); /* Right-associative */
		p->depth--;
		if (p->err != PARSE_OK) return 0.0;
		errno = 0;
		double res = pow(left, right);
		if (isnan(res)) {
			p->err = PARSE_ERR_DOMAIN;
			return 0.0;
		}
		if (!isfinite(res)) {
			p->err = PARSE_ERR_OVERFLOW;
			return 0.0;
		}
		return res;
	}
	return left;
}

/* Check if upcoming token starts an implicit multiplication: e.g. 2(3), 2π, 2sin(x) */
static bool starts_implicit_mul(Parser *p) {
	skip_spaces(p);
	const char *s = p->src + p->pos;
	if (*s == '(') return true;
	if (strncmp(s, "π", strlen("π")) == 0 || strncmp(s, "√", strlen("√")) == 0) return true;
	if (isalpha((unsigned char)*s)) return true;
	return false;
}

static double parse_term(Parser *p) {
	double left = parse_power(p);
	while (p->err == PARSE_OK) {
		if (match_str(p, "*") || match_str(p, "×")) {
			double right = parse_power(p);
			left *= right;
		} else if (match_str(p, "/") || match_str(p, "÷")) {
			double right = parse_power(p);
			if (p->err != PARSE_OK) return 0.0;
			if (right == 0.0) {
				p->err = PARSE_ERR_DIV_ZERO;
				return 0.0;
			}
			left /= right;
		} else if (starts_implicit_mul(p)) {
			double right = parse_power(p);
			left *= right;
		} else {
			break;
		}
		if (!isfinite(left) && p->err == PARSE_OK) {
			p->err = PARSE_ERR_OVERFLOW;
		}
	}
	return left;
}

static double parse_expr(Parser *p) {
	double left = parse_term(p);
	while (p->err == PARSE_OK) {
		if (match_str(p, "+")) {
			double right = parse_term(p);
			left += right;
		} else if (match_str(p, "-") || match_str(p, "−")) {
			double right = parse_term(p);
			left -= right;
		} else {
			break;
		}
		if (!isfinite(left) && p->err == PARSE_OK) {
			p->err = PARSE_ERR_OVERFLOW;
		}
	}
	return left;
}

static ParseStatus evaluate_expression(const char *expr, bool deg_mode, double *out_val) {
	Parser p;
	p.src = expr;
	p.pos = 0;
	p.depth = 0;
	p.deg_mode = deg_mode;
	p.err = PARSE_OK;

	skip_spaces(&p);
	if (p.src[p.pos] == '\0') {
		*out_val = 0.0;
		return PARSE_OK;
	}

	double val = parse_expr(&p);
	skip_spaces(&p);
	if (p.err == PARSE_OK && p.src[p.pos] != '\0') {
		p.err = PARSE_ERR_SYNTAX;
	}
	if (p.err == PARSE_OK && (!isfinite(val) || isnan(val))) {
		p.err = PARSE_ERR_OVERFLOW;
	}
	if (p.err == PARSE_OK && fabs(val) < 1e-15) {
		val = 0.0; /* Avoid -0.0 */
	}
	*out_val = val;
	return p.err;
}

static void format_number(double val, char *buf, size_t buflen) {
	if (fabs(val) < 1e-15) val = 0.0;
	snprintf(buf, buflen, "%.12g", val);
}

/* ---------------- UI & State Management ---------------- */

static void update_display(CalcApp *app) {
	gtk_label_set_text(GTK_LABEL(app->expr_label), app->expr);
	gtk_label_set_text(GTK_LABEL(app->result_label), app->current_result);
}

static void refresh_history_ui(CalcApp *app) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(app->history_list));
	for (GList *it = children; it != NULL; it = it->next) {
		gtk_widget_destroy(GTK_WIDGET(it->data));
	}
	g_list_free(children);

	for (size_t i = app->history_count; i > 0; i--) {
		HistoryItem *item = &app->history[i - 1];
		GtkWidget *row = gtk_list_box_row_new();
		GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		gtk_style_context_add_class(gtk_widget_get_style_context(vbox), "history-row");

		char eline[MAX_EXPR_LEN + 8];
		snprintf(eline, sizeof(eline), "%s =", item->expr);
		GtkWidget *le = gtk_label_new(eline);
		gtk_label_set_xalign(GTK_LABEL(le), 1.0f);
		gtk_label_set_ellipsize(GTK_LABEL(le), PANGO_ELLIPSIZE_START);
		gtk_style_context_add_class(gtk_widget_get_style_context(le), "history-expr");

		GtkWidget *lv = gtk_label_new(item->result);
		gtk_label_set_xalign(GTK_LABEL(lv), 1.0f);
		gtk_style_context_add_class(gtk_widget_get_style_context(lv), "history-val");

		gtk_box_pack_start(GTK_BOX(vbox), le, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), lv, FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(row), vbox);
		g_object_set_data(G_OBJECT(row), "history-idx", GINT_TO_POINTER((int)(i - 1)));
		gtk_container_add(GTK_CONTAINER(app->history_list), row);
	}
	gtk_widget_show_all(app->history_list);
}

static void push_history(CalcApp *app, const char *expr, const char *result, double value) {
	if (app->history_count >= MAX_HISTORY) {
		memmove(&app->history[0], &app->history[1], (MAX_HISTORY - 1) * sizeof(HistoryItem));
		app->history_count = MAX_HISTORY - 1;
	}
	HistoryItem *slot = &app->history[app->history_count];
	snprintf(slot->expr, sizeof(slot->expr), "%s", expr);
	snprintf(slot->result, sizeof(slot->result), "%s", result);
	slot->value = value;
	app->history_count++;
	refresh_history_ui(app);
}

static void append_to_expr(CalcApp *app, const char *token) {
	if (app->has_error) {
		app->expr[0] = '\0';
		snprintf(app->current_result, sizeof(app->current_result), "0");
		app->has_error = false;
		app->just_evaluated = false;
	}

	bool is_binary_op = (strcmp(token, "+") == 0 || strcmp(token, "-") == 0 ||
	                     strcmp(token, "×") == 0 || strcmp(token, "÷") == 0 ||
	                     strcmp(token, "^") == 0);
	bool is_postfix_op = (strcmp(token, "!") == 0 || strcmp(token, "%") == 0);

	if (app->just_evaluated) {
		if (is_binary_op || is_postfix_op) {
			snprintf(app->expr, sizeof(app->expr), "%s", app->current_result);
		} else {
			app->expr[0] = '\0';
		}
		app->just_evaluated = false;
	}

	size_t cur_len = strlen(app->expr);
	size_t tok_len = strlen(token);
	if (cur_len + tok_len + 1 < sizeof(app->expr)) {
		memcpy(app->expr + cur_len, token, tok_len + 1);
	}

	/* Live preview evaluation when valid, else show current expression tail */
	double preview = 0.0;
	if (evaluate_expression(app->expr, app->deg_mode, &preview) == PARSE_OK) {
		format_number(preview, app->current_result, sizeof(app->current_result));
	} else {
		snprintf(app->current_result, sizeof(app->current_result), "%s", app->expr);
	}
	update_display(app);
}

static void clear_all(CalcApp *app) {
	app->expr[0] = '\0';
	snprintf(app->current_result, sizeof(app->current_result), "0");
	app->just_evaluated = false;
	app->has_error = false;
	update_display(app);
}

static void clear_entry(CalcApp *app) {
	if (app->just_evaluated || app->has_error) {
		clear_all(app);
		return;
	}
	/* Trim trailing number/identifier from expression */
	size_t len = strlen(app->expr);
	while (len > 0 && (isalnum((unsigned char)app->expr[len - 1]) || app->expr[len - 1] == '.')) {
		len--;
	}
	app->expr[len] = '\0';
	snprintf(app->current_result, sizeof(app->current_result), "0");
	update_display(app);
}

static void backspace_expr(CalcApp *app) {
	if (app->just_evaluated || app->has_error) {
		clear_all(app);
		return;
	}
	size_t len = strlen(app->expr);
	if (len == 0) return;

	/* UTF-8 aware backspace */
	char *prev = g_utf8_find_prev_char(app->expr, app->expr + len);
	if (prev) {
		*prev = '\0';
	} else {
		app->expr[len - 1] = '\0';
	}

	if (app->expr[0] == '\0') {
		snprintf(app->current_result, sizeof(app->current_result), "0");
	} else {
		double preview = 0.0;
		if (evaluate_expression(app->expr, app->deg_mode, &preview) == PARSE_OK) {
			format_number(preview, app->current_result, sizeof(app->current_result));
		} else {
			snprintf(app->current_result, sizeof(app->current_result), "%s", app->expr);
		}
	}
	update_display(app);
}

static void do_evaluate(CalcApp *app) {
	if (app->expr[0] == '\0') return;

	double val = 0.0;
	ParseStatus st = evaluate_expression(app->expr, app->deg_mode, &val);
	if (st == PARSE_OK) {
		char saved_expr[MAX_EXPR_LEN];
		snprintf(saved_expr, sizeof(saved_expr), "%s", app->expr);
		format_number(val, app->current_result, sizeof(app->current_result));
		app->last_value = val;
		app->has_last_value = true;
		app->just_evaluated = true;
		app->has_error = false;
		push_history(app, saved_expr, app->current_result, val);
	} else {
		app->has_error = true;
		app->just_evaluated = true;
		switch (st) {
		case PARSE_ERR_DIV_ZERO:
			snprintf(app->current_result, sizeof(app->current_result), "Cannot divide by zero");
			break;
		case PARSE_ERR_DOMAIN:
			snprintf(app->current_result, sizeof(app->current_result), "Invalid domain");
			break;
		case PARSE_ERR_OVERFLOW:
			snprintf(app->current_result, sizeof(app->current_result), "Overflow");
			break;
		case PARSE_ERR_DEPTH:
			snprintf(app->current_result, sizeof(app->current_result), "Nesting too deep");
			break;
		default:
			snprintf(app->current_result, sizeof(app->current_result), "Syntax error");
			break;
		}
	}
	update_display(app);
}

static void wrap_unary_action(CalcApp *app, const char *fn_prefix, const char *fn_suffix) {
	if (app->has_error) clear_all(app);
	const char *target = (app->expr[0] != '\0' && !app->just_evaluated)
		? app->expr : app->current_result;
	char wrapped[MAX_EXPR_LEN];
	snprintf(wrapped, sizeof(wrapped), "%s%s%s", fn_prefix, target, fn_suffix);
	snprintf(app->expr, sizeof(app->expr), "%s", wrapped);
	app->just_evaluated = false;
	do_evaluate(app);
}

static void toggle_sign(CalcApp *app) {
	if (app->has_error) {
		clear_all(app);
		return;
	}
	if (app->just_evaluated || app->expr[0] == '\0') {
		if (strcmp(app->current_result, "0") == 0) return;
		if (app->current_result[0] == '-') {
			memmove(app->current_result, app->current_result + 1, strlen(app->current_result));
		} else {
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "-%.60s", app->current_result);
			snprintf(app->current_result, sizeof(app->current_result), "%s", tmp);
		}
		snprintf(app->expr, sizeof(app->expr), "%s", app->current_result);
		update_display(app);
		return;
	}
	char wrapped[MAX_EXPR_LEN];
	if (strncmp(app->expr, "-(", 2) == 0 && app->expr[strlen(app->expr) - 1] == ')') {
		size_t inner = strlen(app->expr) - 3;
		snprintf(wrapped, sizeof(wrapped), "%.*s", (int)inner, app->expr + 2);
	} else {
		snprintf(wrapped, sizeof(wrapped), "-(%s)", app->expr);
	}
	snprintf(app->expr, sizeof(app->expr), "%s", wrapped);
	double preview = 0.0;
	if (evaluate_expression(app->expr, app->deg_mode, &preview) == PARSE_OK) {
		format_number(preview, app->current_result, sizeof(app->current_result));
	}
	update_display(app);
}

static void copy_result_to_clipboard(CalcApp *app) {
	GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	if (cb) {
		gtk_clipboard_set_text(cb, app->current_result, -1);
	}
}

static void on_calc_button_clicked(GtkButton *btn, gpointer user_data) {
	CalcApp *app = user_data;
	const char *action = g_object_get_data(G_OBJECT(btn), "calc-action");
	if (!action) return;

	if (strcmp(action, "C") == 0) {
		clear_all(app);
	} else if (strcmp(action, "CE") == 0) {
		clear_entry(app);
	} else if (strcmp(action, "BS") == 0) {
		backspace_expr(app);
	} else if (strcmp(action, "=") == 0) {
		do_evaluate(app);
	} else if (strcmp(action, "1/x") == 0) {
		wrap_unary_action(app, "1/(", ")");
	} else if (strcmp(action, "x2") == 0) {
		wrap_unary_action(app, "(", ")^2");
	} else if (strcmp(action, "sqrt") == 0) {
		wrap_unary_action(app, "sqrt(", ")");
	} else if (strcmp(action, "neg") == 0) {
		toggle_sign(app);
	} else if (strcmp(action, "fact") == 0) {
		append_to_expr(app, "!");
	} else {
		append_to_expr(app, action);
	}
}

static GtkWidget *make_calc_btn(CalcApp *app, const char *label, const char *action, const char *css_class) {
	GtkWidget *btn = gtk_button_new_with_label(label);
	GtkStyleContext *sc = gtk_widget_get_style_context(btn);
	gtk_style_context_add_class(sc, "calc-btn");
	if (css_class) {
		gtk_style_context_add_class(sc, css_class);
	}
	gtk_widget_set_can_focus(btn, FALSE);
	g_object_set_data(G_OBJECT(btn), "calc-action", (gpointer)action);
	g_signal_connect(btn, "clicked", G_CALLBACK(on_calc_button_clicked), app);
	return btn;
}

static void on_mode_toggled(GtkButton *btn, gpointer user_data) {
	(void)btn;
	CalcApp *app = user_data;
	app->scientific_mode = !app->scientific_mode;
	gtk_revealer_set_reveal_child(GTK_REVEALER(app->sci_revealer), app->scientific_mode);
	gtk_button_set_label(GTK_BUTTON(app->mode_btn),
		app->scientific_mode ? "Mode: Scientific" : "Mode: Standard");
}

static void on_deg_rad_toggled(GtkButton *btn, gpointer user_data) {
	(void)btn;
	CalcApp *app = user_data;
	app->deg_mode = !app->deg_mode;
	gtk_button_set_label(GTK_BUTTON(app->deg_rad_btn), app->deg_mode ? "DEG" : "RAD");
	if (app->expr[0] != '\0' && !app->has_error) {
		double preview = 0.0;
		if (evaluate_expression(app->expr, app->deg_mode, &preview) == PARSE_OK) {
			format_number(preview, app->current_result, sizeof(app->current_result));
			update_display(app);
		}
	}
}

static void on_history_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
	(void)box;
	CalcApp *app = user_data;
	if (!row) return;
	int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "history-idx"));
	if (idx >= 0 && (size_t)idx < app->history_count) {
		HistoryItem *item = &app->history[idx];
		snprintf(app->expr, sizeof(app->expr), "%s", item->expr);
		snprintf(app->current_result, sizeof(app->current_result), "%s", item->result);
		app->last_value = item->value;
		app->has_last_value = true;
		app->just_evaluated = true;
		app->has_error = false;
		update_display(app);
	}
}

static void on_clear_history_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	CalcApp *app = user_data;
	app->history_count = 0;
	refresh_history_ui(app);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *ev, gpointer user_data) {
	(void)widget;
	CalcApp *app = user_data;
	GdkModifierType mods = ev->state & gtk_accelerator_get_default_mod_mask();
	guint kv = ev->keyval;

	if ((mods & GDK_CONTROL_MASK) && (gdk_keyval_to_lower(kv) == GDK_KEY_c)) {
		copy_result_to_clipboard(app);
		return TRUE;
	}

	if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter || kv == GDK_KEY_equal) {
		do_evaluate(app);
		return TRUE;
	}
	if (kv == GDK_KEY_BackSpace) {
		backspace_expr(app);
		return TRUE;
	}
	if (kv == GDK_KEY_Escape) {
		clear_all(app);
		return TRUE;
	}
	if (kv == GDK_KEY_Delete) {
		clear_entry(app);
		return TRUE;
	}

	/* Digits */
	if (kv >= GDK_KEY_0 && kv <= GDK_KEY_9) {
		char s[2] = { (char)('0' + (kv - GDK_KEY_0)), '\0' };
		append_to_expr(app, s);
		return TRUE;
	}
	if (kv >= GDK_KEY_KP_0 && kv <= GDK_KEY_KP_9) {
		char s[2] = { (char)('0' + (kv - GDK_KEY_KP_0)), '\0' };
		append_to_expr(app, s);
		return TRUE;
	}

	switch (kv) {
	case GDK_KEY_plus:
	case GDK_KEY_KP_Add:
		append_to_expr(app, "+");
		return TRUE;
	case GDK_KEY_minus:
	case GDK_KEY_KP_Subtract:
		append_to_expr(app, "-");
		return TRUE;
	case GDK_KEY_asterisk:
	case GDK_KEY_KP_Multiply:
		append_to_expr(app, "×");
		return TRUE;
	case GDK_KEY_slash:
	case GDK_KEY_KP_Divide:
		append_to_expr(app, "÷");
		return TRUE;
	case GDK_KEY_period:
	case GDK_KEY_KP_Decimal:
		append_to_expr(app, ".");
		return TRUE;
	case GDK_KEY_percent:
		append_to_expr(app, "%");
		return TRUE;
	case GDK_KEY_asciicircum:
		append_to_expr(app, "^");
		return TRUE;
	case GDK_KEY_exclam:
		append_to_expr(app, "!");
		return TRUE;
	case GDK_KEY_parenleft:
		append_to_expr(app, "(");
		return TRUE;
	case GDK_KEY_parenright:
		append_to_expr(app, ")");
		return TRUE;
	case GDK_KEY_p:
		append_to_expr(app, "π");
		return TRUE;
	case GDK_KEY_e:
		append_to_expr(app, "e");
		return TRUE;
	default:
		break;
	}

	return FALSE;
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-calc");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(CALC_EXTRA_CSS);

	CalcApp app;
	memset(&app, 0, sizeof(app));
	snprintf(app.current_result, sizeof(app.current_result), "0");
	app.deg_mode = true;
	app.scientific_mode = false;

	app.window = diamonds_gtk_window_new("Diamonds Calculator", 580, 520);
	g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

	/* HeaderBar */
	GtkWidget *header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Calculator");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), header);

	app.mode_btn = gtk_button_new_with_label("Mode: Standard");
	g_signal_connect(app.mode_btn, "clicked", G_CALLBACK(on_mode_toggled), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app.mode_btn);

	app.deg_rad_btn = gtk_button_new_with_label("DEG");
	g_signal_connect(app.deg_rad_btn, "clicked", G_CALLBACK(on_deg_rad_toggled), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app.deg_rad_btn);

	GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
	g_signal_connect_swapped(copy_btn, "clicked", G_CALLBACK(copy_result_to_clipboard), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), copy_btn);

	/* Main Horizontal Box: Left = Calculator, Right = History Panel */
	GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(main_hbox), 12);
	gtk_container_add(GTK_CONTAINER(app.window), main_hbox);

	GtkWidget *calc_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(main_hbox), calc_vbox, TRUE, TRUE, 0);

	/* Display Card */
	GtkWidget *disp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(disp_box), "calc-display");

	app.expr_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.expr_label), 1.0f);
	gtk_label_set_ellipsize(GTK_LABEL(app.expr_label), PANGO_ELLIPSIZE_START);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.expr_label), "calc-expr");

	app.result_label = gtk_label_new("0");
	gtk_label_set_xalign(GTK_LABEL(app.result_label), 1.0f);
	gtk_label_set_ellipsize(GTK_LABEL(app.result_label), PANGO_ELLIPSIZE_START);
	gtk_label_set_selectable(GTK_LABEL(app.result_label), FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.result_label), "calc-result");

	gtk_box_pack_start(GTK_BOX(disp_box), app.expr_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(disp_box), app.result_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(calc_vbox), disp_box, FALSE, FALSE, 0);

	/* Scientific Mode Revealer Grid */
	app.sci_revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(app.sci_revealer),
		GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);

	GtkWidget *sci_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(sci_grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(sci_grid), 6);
	gtk_grid_set_column_homogeneous(GTK_GRID(sci_grid), TRUE);

	struct {
		const char *label;
		const char *action;
		int col;
		int row;
	} sci_btns[] = {
		{ "sin",  "sin(",  0, 0 },
		{ "cos",  "cos(",  1, 0 },
		{ "tan",  "tan(",  2, 0 },
		{ "π",    "π",     3, 0 },
		{ "asin", "asin(", 0, 1 },
		{ "acos", "acos(", 1, 1 },
		{ "atan", "atan(", 2, 1 },
		{ "e",    "e",     3, 1 },
		{ "ln",   "ln(",   0, 2 },
		{ "log",  "log(",  1, 2 },
		{ "x^y",  "^",     2, 2 },
		{ "n!",   "fact",  3, 2 },
		{ "(",    "(",     0, 3 },
		{ ")",    ")",     1, 3 },
	};
	for (size_t i = 0; i < sizeof(sci_btns) / sizeof(sci_btns[0]); i++) {
		GtkWidget *b = make_calc_btn(&app, sci_btns[i].label, sci_btns[i].action, "calc-sci");
		gtk_grid_attach(GTK_GRID(sci_grid), b, sci_btns[i].col, sci_btns[i].row, 1, 1);
	}
	gtk_container_add(GTK_CONTAINER(app.sci_revealer), sci_grid);
	gtk_box_pack_start(GTK_BOX(calc_vbox), app.sci_revealer, FALSE, FALSE, 0);

	/* Standard Mode Grid (Win11 4x6 Layout) */
	GtkWidget *std_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(std_grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(std_grid), 6);
	gtk_grid_set_row_homogeneous(GTK_GRID(std_grid), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(std_grid), TRUE);

	struct {
		const char *label;
		const char *action;
		const char *cls;
		int col;
		int row;
	} std_btns[] = {
		{ "%",   "%",    NULL,       0, 0 },
		{ "CE",  "CE",   NULL,       1, 0 },
		{ "C",   "C",    NULL,       2, 0 },
		{ "⌫",   "BS",   NULL,       3, 0 },

		{ "1/x", "1/x",  NULL,       0, 1 },
		{ "x²",  "x2",   NULL,       1, 1 },
		{ "√x",  "sqrt", NULL,       2, 1 },
		{ "÷",   "÷",    NULL,       3, 1 },

		{ "7",   "7",    "calc-num", 0, 2 },
		{ "8",   "8",    "calc-num", 1, 2 },
		{ "9",   "9",    "calc-num", 2, 2 },
		{ "×",   "×",    NULL,       3, 2 },

		{ "4",   "4",    "calc-num", 0, 3 },
		{ "5",   "5",    "calc-num", 1, 3 },
		{ "6",   "6",    "calc-num", 2, 3 },
		{ "−",   "-",    NULL,       3, 3 },

		{ "1",   "1",    "calc-num", 0, 4 },
		{ "2",   "2",    "calc-num", 1, 4 },
		{ "3",   "3",    "calc-num", 2, 4 },
		{ "+",   "+",    NULL,       3, 4 },

		{ "±",   "neg",  "calc-num", 0, 5 },
		{ "0",   "0",    "calc-num", 1, 5 },
		{ ".",   ".",    "calc-num", 2, 5 },
		{ "=",   "=",    "calc-eq",  3, 5 },
	};
	for (size_t i = 0; i < sizeof(std_btns) / sizeof(std_btns[0]); i++) {
		GtkWidget *b = make_calc_btn(&app, std_btns[i].label, std_btns[i].action, std_btns[i].cls);
		gtk_grid_attach(GTK_GRID(std_grid), b, std_btns[i].col, std_btns[i].row, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(calc_vbox), std_grid, TRUE, TRUE, 0);

	/* History Panel */
	GtkWidget *hist_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_size_request(hist_vbox, 190, -1);
	gtk_style_context_add_class(gtk_widget_get_style_context(hist_vbox), "history-panel");

	GtkWidget *hist_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *hist_title = gtk_label_new("History");
	gtk_style_context_add_class(gtk_widget_get_style_context(hist_title), "card-title");
	GtkWidget *clear_hist_btn = gtk_button_new_with_label("Clear");
	g_signal_connect(clear_hist_btn, "clicked", G_CALLBACK(on_clear_history_clicked), &app);
	gtk_box_pack_start(GTK_BOX(hist_hdr), hist_title, FALSE, FALSE, 4);
	gtk_box_pack_end(GTK_BOX(hist_hdr), clear_hist_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hist_vbox), hist_hdr, FALSE, FALSE, 0);

	GtkWidget *hist_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(hist_scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	app.history_list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.history_list), GTK_SELECTION_SINGLE);
	g_signal_connect(app.history_list, "row-activated", G_CALLBACK(on_history_row_activated), &app);
	gtk_container_add(GTK_CONTAINER(hist_scroll), app.history_list);
	gtk_box_pack_start(GTK_BOX(hist_vbox), hist_scroll, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(main_hbox), hist_vbox, FALSE, TRUE, 0);

	gtk_widget_show_all(app.window);
	gtk_revealer_set_reveal_child(GTK_REVEALER(app.sci_revealer), FALSE);

	gtk_main();
	return 0;
}
