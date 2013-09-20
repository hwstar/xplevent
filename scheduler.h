#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef struct SchedListEntry_s{
  time_t time;
  uint32_t magic;
  Bool hasRun;
  String name;
  String scriptName
  struct SchedListEntry_s *next;
} SchedInfo_t;

typedef SchedListEntry_t * SchedListEntryPtr_t;

typedef struct SchedInfo_s{
  SchedListEntryPtr_t head;
} SchedInfo_t;

typedef SchedInfo_t * SchedInfoPtr_t;

#endif
