#ifndef DEV_FLOW_PLAYER_H
#define DEV_FLOW_PLAYER_H


#define DEV_FLOW_PLAYER_VOLUME_NODE_NAME    "74E325"

int dev_flow_player_open(u8 ch_num, u16 source_uuid);

void dev_flow_player_close(void);
/* app_core: 0 = stopping, 1 = ready to close, negative = retry later. */
int dev_flow_player_drain_close(void);

bool dev_flow_player_runing();



#endif
