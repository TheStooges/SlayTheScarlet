/* The effect probe of debug builds (effect_probe.cpp). Release builds make this do nothing. */
#ifndef STS_EFFECT_PROBE_H
#define STS_EFFECT_PROBE_H

/* Called on every battle frame. Writes effects.log in Practice mode. */
void effect_probe_frame(void);

#endif
