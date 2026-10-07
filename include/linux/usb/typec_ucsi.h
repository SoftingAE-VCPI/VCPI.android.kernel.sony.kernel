/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __TYPEC_UCSI_H__
#define __TYPEC_UCSI_H__

struct ucsi_power_ops {
	void (*disable_otg)(void);
	void (*enable_otg)(void);
	int (*is_power_supplied)(void);
};

#if IS_ENABLED(CONFIG_TYPEC_UCSI)
void ucsi_register_power_ops(const struct ucsi_power_ops *ops);
void ucsi_unregister_power_ops(void);
#else
static inline void ucsi_register_power_ops(const struct ucsi_power_ops *ops) {}
static inline void ucsi_unregister_power_ops(void) {}
#endif

#endif /* __TYPEC_UCSI_H__ */
