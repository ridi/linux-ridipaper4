#ifndef __SOUND_AUD3004X_H__
#define __SOUND_AUD3004X_H__

extern bool aud3004x_notifier_check(void);
extern void aud3004x_call_notifier(u8 irq_codec[], int count);

#endif /* __SOUND_AUD3004X_H__ */
