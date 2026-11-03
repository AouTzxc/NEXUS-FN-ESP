#include "rebootnova/model_chams_transform.h"
typedef void (*Setter)(void*,const Transform*,U32,void*,U32);
#ifndef RN_CHAMS_EMBEDDED
__attribute__((section(".data.state"))) volatile State state;
#endif
typedef void (*MaterialSetter)(void*,U32,void*);
static U32 at32(U64 a){return *(volatile U32*)a;}
static U64 at64(U64 a){return *(volatile U64*)a;}
static int live(U64 a,U32 index){return a>0x100000000ULL && a<0x8000000000ULL && !(a&7) && at32(a+0x14)==index && !(at32(a+0x10)&0x18000);}

typedef U64 (*CreateMID)(void*,void*,U32);
typedef void (*SetVector)(void*,const void*,float,float,float,float);
typedef void (*MarkDirty)(void*);
static int partLive(Part *p){return live(p->mesh,p->index) && at64(p->mesh+0x20)==p->klass;}
// Array reads are bounded and refreshed after every setter (UE may reallocate).
static int slotValue(Part *p,U32 k,U64 *value){
 U64 array=at64(p->mesh+0x570);U32 num=at32(p->mesh+0x578),max=at32(p->mesh+0x57c);
 if(num>16 || max<num)return 0;
 if(k>=num){*value=0;return 1;}
 if(array<=0x100000000ULL || array>=0x8000000000ULL || (array&7))return 0;
 *value=at64(array+k*8);return 1;
}
static int originalRelative(Part *p){
 double v[9];const volatile double *raw=(const volatile double*)(p->mesh+0x138);
 for(U32 i=0;i<9;i++)v[i]=raw[i];
 return rn_chams_relative_equal(v,&p->original);
}
static void restore(State *s){
 s->enabled=0;
 if(s->restored)return;
 // Error bits describe the current attempt. Pending masks persist until verified.
 s->restoreErrors=0;
 if(s->count>4){s->restoreErrors=0x80000000U;return;}
 for(U32 i=0;i<s->count;i++){
  Part *p=&s->part[i];U32 oldCount=rn_chams_original_count(s,i),n=s->materialCount[i],partMask=0xffU<<(i*8),transformBit=1U<<i;
  if(!((s->appliedMask|s->backupMask)&partMask) && !(s->transformMask&transformBit))continue;
  if(!partLive(p)){s->restoreErrors|=partMask;continue;}
  if(s->tintMaterial && at64(p->mesh+0x580)==s->tintMaterial){
   *(volatile U64*)(p->mesh+0x580)=0;
   if(s->markDirty)((MarkDirty)s->markDirty)((void*)p->mesh);
  }
  if(n>8 || oldCount>n){s->restoreErrors|=partMask;continue;}
  for(U32 k=0;k<n;k++){
   U32 mask=1U<<(i*8+k);U64 current=0;
   if(!(s->appliedMask&mask))continue;
   U64 old=s->originalMaterial[i][k];
   if(!slotValue(p,k,&current) || (old && !live(old,s->originalMaterialIndex[i][k]))){s->restoreErrors|=mask;continue;}
   // Never overwrite a newer owner, even when our original remains alive.
   if(current!=old && current!=s->tintMaterial){s->restoreErrors|=mask;continue;}
   if(current!=old)((MaterialSetter)s->setMaterial)((void*)p->mesh,k,(void*)old);
   if(slotValue(p,k,&current) && current==old)s->appliedMask&=~mask;
   else s->restoreErrors|=mask;
  }
  if(!(s->appliedMask&partMask) && (s->backupMask&partMask)){
   U32 before=at32(p->mesh+0x578);
   if(before<oldCount || before>2*n){s->restoreErrors|=partMask;}
   else {
    for(U32 k=0;k<n;k++){
     U32 mask=1U<<(i*8+k);U64 backup=0,current=0;
     if(!(s->backupMask&mask))continue;
     if(!slotValue(p,k,&current) || current!=s->originalMaterial[i][k] ||
        !slotValue(p,n+k,&backup) || (backup && backup!=s->originalMaterial[i][k])){s->restoreErrors|=mask;continue;}
     if(backup)((MaterialSetter)s->setMaterial)((void*)p->mesh,n+k,(void*)0);
     if(slotValue(p,n+k,&backup) && !backup)s->backupMask&=~mask;
     else s->restoreErrors|=mask;
    }
    if(!(s->backupMask&partMask)){
     *(volatile U32*)(p->mesh+0x578)=oldCount;
     ((MarkDirty)s->markDirty)((void*)p->mesh);
    }
   }
  }
  if(s->transformMask&transformBit){
   volatile U16 *f=(volatile U16*)(p->mesh+0x198);
   *f=(*f & ~0x2000U)|(p->parentBounds&0x2000);
   if(!rn_chams_transform_valid(&p->original)){s->restoreErrors|=partMask;continue;}
   ((Setter)s->setRelative)((void*)p->mesh,&p->original,0,(void*)0,1);
   if(originalRelative(p) && ((*f&0x2000)==(p->parentBounds&0x2000)))s->transformMask&=~transformBit;
   else s->restoreErrors|=partMask;
  }
 }

 // An acknowledgement is forbidden while unresolved ownership remains.
 __asm__ volatile("dmb ish" ::: "memory");
 s->restored=!(s->appliedMask || s->backupMask || s->transformMask || s->restoreErrors);
}
static int applyColor(State *s){
 if(s->colorReady)return 1;
 if(!s->count || s->count>4)return 0;
 for(U32 i=0;i<s->count;i++)for(U32 j=0;j<i;j++)if(s->part[i].mesh==s->part[j].mesh)return 0;
 if(!live(s->paletteParent,s->paletteIndex))return 0;
 // Engine-owned material arrays keep originals reachable while replaced. Never pin raw pointers alone.
 for(U32 i=0;i<s->count && i<4;i++){
  Part *p=&s->part[i];U32 n=s->materialCount[i],oldCount=rn_chams_original_count(s,i);
  if(!partLive(p) || !n || n>8 || oldCount>n || at32(p->mesh+0x578)!=oldCount)return 0;
  // MaterialOnly tints in place. XRAY/projection need the captured relative for restore.
  if(!rn_chams_material_only(s) && (!rn_chams_transform_valid(&p->original) || !originalRelative(p)))return 0;
  for(U32 k=0;k<n;k++){
   U64 current=0,old=s->originalMaterial[i][k];
   if(!slotValue(p,k,&current) || current!=old || (old && !live(old,s->originalMaterialIndex[i][k])) || (k>=oldCount && old))return 0;
  }
  for(U32 k=0;k<n;k++){
   s->backupMask|=1U<<(i*8+k);
   if(s->originalMaterial[i][k])((MaterialSetter)s->setMaterial)((void*)p->mesh,n+k,(void*)s->originalMaterial[i][k]);
   U64 held=0;if(!slotValue(p,n+k,&held) || held!=s->originalMaterial[i][k])return 0;
  }
 }
 s->tintMaterial=((CreateMID)s->createMid)((void*)s->paletteParent,(void*)s->part[0].mesh,0);
 if(!s->tintMaterial || at64(s->tintMaterial+0x1a0)!=s->paletteParent)return 0;
 s->tintIndex=at32(s->tintMaterial+0x14);
 ((SetVector)s->setVector)((void*)s->tintMaterial,s->colorInfo,s->color[0],s->color[1],s->color[2],s->color[3]);
 for(U32 i=0;i<s->count && i<4;i++)for(U32 k=0;k<s->materialCount[i];k++){
  ((MaterialSetter)s->setMaterial)((void*)s->part[i].mesh,k,(void*)s->tintMaterial);
  s->appliedMask|=1U<<(i*8+k);
  U64 current=0;if(!slotValue(&s->part[i],k,&current) || current!=s->tintMaterial)return 0;
 }
 for(U32 i=0;i<s->count && i<4;i++){
  Part *p=&s->part[i];
  if(!partLive(p))continue;
  if(s->markDirty)((MarkDirty)s->markDirty)((void*)p->mesh);
 }
 s->colorReady=1;return 1;
}
typedef U32 (*LineOfSight)(void*,void*,U32,double,double,double);
static void sampleVisibility(State *s){
 if(!s->distinguish){s->lastVisibility=0;s->pendingHits=0;s->nextSampleFrame=0;return;}
 if(!s->lineOfSight || !live(s->controller,s->controllerIndex)){
  s->lastVisibility=0;s->pendingHits=0;return;
 }
 if(s->frames<s->nextSampleFrame)return;
 const volatile double *cam=(const volatile double*)(s->camera+0x29d0);
 // V/H: CL57566230 base AController LoS 0x9003A48. The Fortnite virtual
 // override 0xD20657C always returns true and MUST NOT be used here.
 U32 value=((LineOfSight)s->lineOfSight)((void*)s->controller,(void*)s->actor,0,cam[0],cam[1],cam[2])&1U;
 U32 observed=value?1:2;
 s->sampleCount++;
 U32 interval=s->sampleEvery;
 if(interval<6)interval=6;if(interval>120)interval=120;
 s->nextSampleFrame=s->frames+interval;
 if(observed==s->pendingVisibility)s->pendingHits++;else{s->pendingVisibility=observed;s->pendingHits=1;}
 if(s->pendingHits>=2 || !s->lastVisibility)s->lastVisibility=observed;
}
static int refreshColor(State *s){
 if(!live(s->tintMaterial,s->tintIndex))return 0;
 U32 selected=s->distinguish?s->lastVisibility:0;
 if(s->appliedVisibility==selected && s->appliedRevision==s->configRevision)return 1;
 const float *c=selected==1?s->visibleColor:(selected==2?s->occludedColor:s->color);
 ((SetVector)s->setVector)((void*)s->tintMaterial,s->colorInfo,c[0],c[1],c[2],c[3]);
 s->appliedVisibility=selected;s->appliedRevision=s->configRevision;
 return 1;
}
__attribute__((visibility("hidden"))) void update_proxy(State *s,U64 actor){
 if(actor!=s->actor)return;
 if(s->restored){s->enabled=0;return;} // terminal: reinstallation requires a new state
 s->frames++;
 if(!s->enabled || !s->lease || !live(actor,s->actorIndex) || !live(s->primary,s->primaryIndex) || !live(s->camera,s->cameraIndex) || at64(actor+0x2e0)!=s->primary){restore(s);return;}
 if(!rn_chams_render_mode_valid(s)){restore(s);return;}
 if((at64(s->primary+0x5e0)||at64(s->primary+0x5e8)) && !rn_chams_appearance_projection(s) &&
    (!rn_chams_inplace_tint(s)||!rn_chams_primary_in_plan(s))){restore(s);return;}
 // Camera-pull projection must never move the pose leader, even in a malformed host plan.
 if(!rn_chams_inplace_tint(s))for(U32 i=0;i<s->count && i<4;i++)if(s->part[i].mesh==s->primary){restore(s);return;}
 s->lease--;
 sampleVisibility(s);
 if(!applyColor(s) || !refreshColor(s)){restore(s);return;}
 if(rn_chams_inplace_tint(s)){
  for(U32 i=0;i<s->count && i<4;i++){
   if(!partLive(&s->part[i]) || !s->materialCount[i] || s->materialCount[i]>8){restore(s);return;}
   for(U32 k=0;k<s->materialCount[i];k++){
    U64 current=0;
    if(!slotValue(&s->part[i],k,&current)||current!=s->tintMaterial){restore(s);return;}
   }
  }
  if(rn_chams_material_only(s))return;
 }
 int xray=rn_chams_inplace_xray(s);
 double peel=s->nearDistance,lo=1,hi=xray?2000:200;
 if(!(peel>=lo && peel<=hi)){restore(s);return;}
 for(U32 pass=0;pass<2;pass++){
  for(U32 i=0;i<s->count && i<4;i++){
   Part *p=&s->part[i];
   int pri=p->mesh==s->primary;
   if((pass==0 && !pri)||(pass==1 && pri))continue;
   if(!partLive(p)||!rn_chams_transform_valid(&p->original)){restore(s);return;}
   s->transformMask|=1U<<i;
   ((Setter)s->setRelative)((void*)p->mesh,&p->original,0,(void*)0,1);
   if(!originalRelative(p)){restore(s);return;}
  }
 }
 const volatile double *main=(const volatile double*)(s->primary+0x1e0);
 const volatile double *cam=(const volatile double*)(s->camera+0x29d0);
 double cx=cam[0],cy=cam[1],cz=cam[2],px=main[4],py=main[5],pz=main[6];
 double dx=px-cx,dy=py-cy,dz=pz-cz,d2=dx*dx+dy*dy+dz*dz;
 if(!(d2<1.0e16) || (!xray && d2<=40000.0)){restore(s);return;}
 double dist=__builtin_sqrt(d2),scale=xray?1.0:peel/dist,ox=0,oy=0,oz=0;
 if(xray){double remain=dist-peel;if(remain<dist*0.60)remain=dist*0.60;if(remain<600.0)remain=600.0;if(remain<dist){double move=dist-remain;ox=(cx-px)*(move/dist);oy=(cy-py)*(move/dist);oz=(cz-pz)*(move/dist);}}
 Transform desired[4];
 for(U32 i=0;i<s->count;i++){
  const volatile double *world=(const volatile double*)(s->part[i].mesh+0x1e0);
  for(U32 j=0;j<12;j++)desired[i].v[j]=world[j];
  if(!rn_chams_transform_valid(&desired[i])){restore(s);return;}
  if(xray){desired[i].v[4]+=ox;desired[i].v[5]+=oy;desired[i].v[6]+=oz;}
  else{
   desired[i].v[4]=cx+(desired[i].v[4]-cx)*scale;
   desired[i].v[5]=cy+(desired[i].v[5]-cy)*scale;
   desired[i].v[6]=cz+(desired[i].v[6]-cz)*scale;
   desired[i].v[8]*=scale;desired[i].v[9]*=scale;desired[i].v[10]*=scale;
  }
 }
 for(U32 i=0;i<s->count;i++){
  Part *p=&s->part[i];
  *(volatile U16*)(p->mesh+0x198)&=(U16)~0x2000U;
  ((Setter)s->setWorld)((void*)p->mesh,&desired[i],0,(void*)0,1);
  if(!partLive(p)){restore(s);return;}
  Transform actual;const volatile double *world=(const volatile double*)(p->mesh+0x1e0);
  for(U32 j=0;j<12;j++)actual.v[j]=world[j];
  if(!rn_chams_transform_equal(&actual,&desired[i])){restore(s);return;}
 }
}
