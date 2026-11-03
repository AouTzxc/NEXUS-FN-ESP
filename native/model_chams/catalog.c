// Two original Tick implementations may dispatch into one bounded target catalog.
// H: integration not yet device-validated; offline-only until owner installation exists.
#define RN_CHAMS_EMBEDDED 1
#include "callback.c"
#include "rebootnova/model_chams_multi_abi.h"
__attribute__((section(".data.state"))) volatile MultiState state;
typedef struct { long seconds,nanos; } NativeTime;
typedef int (*ClockFunction)(int,NativeTime*);
static U32 acquire32(U32 *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void release32(U32 *p,U32 n){__atomic_store_n(p,n,__ATOMIC_RELEASE);}
static int retire(MultiState *g,U32 i){
 TargetControl *c=&g->control[i];State *s=&g->target[i];
 restore(s);
 if(s->restored && !s->appliedMask && !s->backupMask && !s->transformMask && !s->restoreErrors){
  c->ackGeneration=c->generation;c->error=0;release32(&c->phase,RN_CHAMS_RESTORED);return 1;
 }
 c->error=s->restoreErrors?s->restoreErrors:1;release32(&c->phase,RN_CHAMS_RETRY);return 0;
}
__attribute__((visibility("hidden"))) void update_catalog(MultiState *g,U64 actor){
 g->callbacks++;
 // No target data is trusted if the catalog ABI is corrupt.
 if(g->magic!=RN_CHAMS_MAGIC || g->targetLimit!=RN_CHAMS_TARGET_LIMIT){
  g->error|=1;release32(&g->enabled,0);release32(&g->restored,0);return;
 }
 if(!acquire32(&g->enabled) && acquire32(&g->restored))return;
 if(acquire32(&g->restored)){release32(&g->enabled,0);return;} // terminal until new owner publication
 if(acquire32(&g->enabled)){
  NativeTime t={0,0};
  if(!g->clockFn || ((ClockFunction)g->clockFn)(1,&t)!=0 || t.seconds<0 || t.nanos<0 || t.nanos>=1000000000L){
   g->error|=2;release32(&g->enabled,0);
  }else{
   U64 now=(U64)t.seconds*1000+(U64)t.nanos/1000000;U32 beat=acquire32(&g->heartbeat),lease=g->leaseMs;
   if(lease<100)lease=100;if(lease>10000)lease=10000;
   if(!g->clockStarted){g->lastBeatMs=now;g->lastTickMs=now;g->lastHeartbeat=beat;g->clockStarted=1;}
   // Reject time rollback before considering a heartbeat; heartbeat cannot hide it.
   if(now<g->lastTickMs || now<g->lastBeatMs){g->error|=4;release32(&g->enabled,0);}
   else{
    if(beat!=g->lastHeartbeat){g->lastHeartbeat=beat;g->lastBeatMs=now;}
    if(now-g->lastBeatMs>=lease){g->error|=8;release32(&g->enabled,0);}
   }
   g->lastTickMs=now;
  }
 }
 U32 enabled=acquire32(&g->enabled),allRestored=1;
 for(U32 i=0;i<RN_CHAMS_TARGET_LIMIT;i++){
  TargetControl *c=&g->control[i];U32 phase=acquire32(&c->phase);
  if(phase==RN_CHAMS_EMPTY)continue;
  if(phase==RN_CHAMS_RESTORED){
   if(c->ackGeneration!=c->generation){g->error|=16;allRestored=0;}
   continue;
  }
  if(phase!=RN_CHAMS_ACTIVE && phase!=RN_CHAMS_RETIRE && phase!=RN_CHAMS_RETRY){g->error|=32;allRestored=0;continue;}
  State *s=&g->target[i];
  if(!enabled || phase!=RN_CHAMS_ACTIVE){if(!retire(g,i))allRestored=0;continue;}
  allRestored=0;
  // Duplicated catalogs cannot apply the same actor twice.
  if(actor!=s->actor)continue;
  if(actor==g->localPawn){g->error|=64;retire(g,i);continue;}
  U32 duplicate=0;
  for(U32 j=0;j<i;j++)if(g->target[j].actor==actor && acquire32(&g->control[j].phase)!=RN_CHAMS_EMPTY){duplicate=1;break;}
  if(duplicate){g->error|=128;retire(g,i);continue;}
  // Catalog wall-clock heartbeat owns lifetime. Do not let the single-target
  // prototype's frame lease silently retire a healthy long-running catalog.
  if(s->enabled && !s->restored)s->lease=1;
  update_proxy(s,actor);
  if(!s->enabled || s->restored)retire(g,i);
 }
 if(!enabled && allRestored)release32(&g->restored,1);
 // Never write vtables; external owner restores hook slots after verified acknowledgements.
}
