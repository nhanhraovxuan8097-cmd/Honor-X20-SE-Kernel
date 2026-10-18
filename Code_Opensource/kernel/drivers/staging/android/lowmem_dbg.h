#ifndef __LMK_DBG_H
#define __LMK_DBG_H

#ifdef CONFIG_LMK_DBG
void lowmem_dbg(short oom_score_adj);
#else
static inline void lowmem_dbg(short oom_score_adj)
{
}
#endif

#endif /* __LMK_DBG_H */
