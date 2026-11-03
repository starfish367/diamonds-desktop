#ifndef DIAMONDS_SOUND_H
#define DIAMONDS_SOUND_H

typedef enum {
	DIAMONDS_SOUND_CLICK,
	DIAMONDS_SOUND_SNAP,
	DIAMONDS_SOUND_ERROR,
} diamonds_sound_type_t;

void diamonds_sound_init(void);
void diamonds_sound_play(diamonds_sound_type_t type);
void diamonds_sound_cleanup(void);

#endif /* DIAMONDS_SOUND_H */
