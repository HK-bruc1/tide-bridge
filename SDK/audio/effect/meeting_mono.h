#ifndef T2620_MEETING_MONO_H
#define T2620_MEETING_MONO_H

/* NODE_IOC_SET_PRIV_FMT, copied before negotiation/start. 0 keeps legacy audio;
 * 1 selects planar MIC0, 2 selects planar MIC3, 3 runs dynamic selection. */
struct meeting_mono_policy {
    unsigned int epoch;
    unsigned char mic;
    void (*fault)(unsigned int epoch, int reason);
};

/* Source health: 0=healthy/inactive, -20=lost PCM, -21=no pair for 500ms. */
int source_dev1_pair_health(void);

#endif
