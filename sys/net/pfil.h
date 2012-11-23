/*	$FreeBSD$ */
/*	$NetBSD: pfil.h,v 1.22 2003/06/23 12:57:08 martin Exp $	*/

/*-
 * Copyright (c) 1996 Matthew R. Green
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NET_PFIL_H_
#define _NET_PFIL_H_

#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/sysctl.h>

struct mbuf;
struct ifnet;
struct inpcb;

typedef	int	(*pfil_func_t)(void *, struct mbuf **, struct ifnet *, int,
		    struct inpcb *);

/*
 * The packet filter hooks are designed for anything to call them to
 * possibly intercept the packet.  Multiple filter hooks are chained
 * together and after each other in the specified order.
 */
struct packet_filter_hook {
        TAILQ_ENTRY(packet_filter_hook) pfil_chain;
	pfil_func_t	 pfil_func;
	void		*pfil_arg;
	int		 pfil_cookie;
	uint8_t		 pfil_order;
	char		*pfil_name;
	struct sysctl_oid *pfil_sysctl_oid;
	struct pfil_head *pfil_head;
};

#define	PFIL_ORDER_FIRST	  0
#define	PFIL_ORDER_DEFAULT	200
#define	PFIL_ORDER_LAST		255

#define PFIL_IN		0x00000001
#define PFIL_OUT	0x00000002
#define PFIL_WAITOK	0x00000004
#define PFIL_ALL	(PFIL_IN|PFIL_OUT)

typedef	TAILQ_HEAD(pfil_chain, packet_filter_hook) pfil_chain_t;

#define	PFIL_TYPE_AF		1	/* key is AF_* type */
#define	PFIL_TYPE_IFNET		2	/* key is ifnet pointer */

#define	PFIL_FLAG_PRIVATE_LOCK	0x01	/* Personal lock instead of global */

/*
 * A pfil head is created by each protocol or packet intercept point.
 * For packet is then run through the hook chain for inspection.
 */
struct pfil_head {
	pfil_chain_t	 ph_in;
	pfil_chain_t	 ph_out;
	int		 ph_type;
	int		 ph_nhooks;
	int		 flags;
	struct rmlock	*ph_plock;	/* Pointer to the used lock */
	struct rmlock	 ph_lock;	/* Private lock storage */
	union {
		u_long		 phu_val;
		void		*phu_ptr;
	} ph_un;
#define	ph_af		ph_un.phu_val
#define	ph_ifnet	ph_un.phu_ptr
	struct sysctl_ctx_list	*ph_sysctl_ctx;
	struct sysctl_oid	*ph_sysctl_oid_in;
	struct sysctl_oid	*ph_sysctl_oid_out;
	LIST_ENTRY(pfil_head) ph_list;
};

/* Public functions for pfil head management by protocols. */
struct	pfil_head *pfil_head_get(int, u_long);
int	pfil_head_register(struct pfil_head *, struct sysctl_oid_list *);
int	pfil_head_unregister(struct pfil_head *);

/* Public functions for pfil hook management by protocols. */
int	pfil_add_hook(pfil_func_t, void *, int, struct pfil_head *);
int	pfil_add_hook_order(pfil_func_t, void *, char *, int, uint8_t,
	    struct pfil_head *);
int	pfil_get_cookie(pfil_func_t, void *, int, struct pfil_head *);
int	pfil_remove_hook(pfil_func_t, void *, int, struct pfil_head *);
#define	PFIL_HOOKED(p) ((p)->ph_nhooks > 0)

/* Public functions to run the packet inspection by protocols. */
int	pfil_run_hooks(struct pfil_head *, struct mbuf **, struct ifnet *,
	    int, struct inpcb *inp);
int	pfil_run_inject(struct pfil_head *, struct mbuf **, struct ifnet *,
	    int, struct inpcb *inp, int cookie);

/* Locking functions for pfil_head to run the hook chain. */
struct	rm_priotracker;	/* Do not require including rmlock header */
int	pfil_try_rlock(struct pfil_head *, struct rm_priotracker *);
void	pfil_rlock(struct pfil_head *, struct rm_priotracker *);
void	pfil_runlock(struct pfil_head *, struct rm_priotracker *);
void	pfil_wlock(struct pfil_head *);
void	pfil_wunlock(struct pfil_head *);
int	pfil_wowned(struct pfil_head *ph);

#define PFIL_TRY_RLOCK(p, t)	rm_try_rlock((p)->ph_plock, (t))
#define PFIL_RLOCK(p, t)	rm_rlock((p)->ph_plock, (t))
#define PFIL_WLOCK(p)		rm_wlock((p)->ph_plock)
#define PFIL_RUNLOCK(p, t)	rm_runlock((p)->ph_plock, (t))
#define PFIL_WUNLOCK(p)		rm_wunlock((p)->ph_plock)
#define PFIL_WOWNED(p)		rm_wowned((p)->ph_plock)

#define	PFIL_LOCK_INIT_REAL(l, t)			\
	rm_init_flags(l, "PFil " t " rmlock", RM_RECURSE)
#define	PFIL_LOCK_DESTROY_REAL(l)			\
	rm_destroy(l)
#define	PFIL_LOCK_INIT(p)	do {			\
	if ((p)->flags & PFIL_FLAG_PRIVATE_LOCK) {	\
		PFIL_LOCK_INIT_REAL(&(p)->ph_lock, "private");	\
		(p)->ph_plock = &(p)->ph_lock;		\
	} else						\
		(p)->ph_plock = &V_pfil_lock;		\
} while (0)
#define	PFIL_LOCK_DESTROY(p)	do {			\
	if ((p)->flags & PFIL_FLAG_PRIVATE_LOCK)	\
		PFIL_LOCK_DESTROY_REAL((p)->ph_plock);	\
} while (0)

/* Internal locking macros for global/vnet pfil_head_list. */
#define PFIL_HEADLIST_LOCK()	mtx_lock(&pfil_global_lock)
#define PFIL_HEADLIST_UNLOCK()	mtx_unlock(&pfil_global_lock)

#endif /* _NET_PFIL_H_ */
