#ifndef _NETLINK_H
#define _NETLINK_H

struct netlink_skb_parms {
	__u32			portid;
};

#define NETLINK_CB(skb)		(*(struct netlink_skb_parms*)&((skb)->cb))

#endif

