#ifndef ASTAIRE_H__
#define ASTAIRE_H__

#include "memcachedstoreview.h"
#include "astaire_statistics.h"
#include "updater.h"
#include "alarm.h"

#include <string>
#include <vector>
#include <map>

class Astaire
{
public:
  Astaire(MemcachedStoreView* view,
          MemcachedConfigReader* view_cfg,
          Alarm* alarm,
          AstaireGlobalStatistics* global_stats,
          AstairePerConnectionStatistics* per_conn_stats,
          std::string self) :
    _view(view),
    _view_cfg(view_cfg),
    _alarm(alarm),
    _global_stats(global_stats),
    _per_conn_stats(per_conn_stats),
    _self(self)
  {
    _updater = new Updater<void, Astaire>(this,
                                          std::mem_fun(&Astaire::trigger_resync));
  };

  ~Astaire()
  {
    delete _updater;
  };

  typedef std::map<std::string, std::vector<uint16_t>> TapList;
  typedef std::map<uint16_t, std::vector<std::string>> OutstandingWorkList;

  struct TapBucketsThreadData
  {
    TapBucketsThreadData(const std::string& tap_server,
                         const std::string& local_server,
                         const std::vector<uint16_t>& buckets,
                         AstaireGlobalStatistics* global_stats,
                         AstairePerConnectionStatistics::ConnectionRecord* conn_stats) :
      tap_server(tap_server),
      local_server(local_server),
      buckets(buckets),
      success(false),
      global_stats(global_stats),
      conn_stats(conn_stats)
    {}

    std::string tap_server;
    std::string local_server;
    std::vector<uint16_t> buckets;
    bool success;
    AstaireGlobalStatistics* global_stats;
    AstairePerConnectionStatistics::ConnectionRecord* conn_stats;
  };

  // Kick off a resync operation.  Astaire will automatically calculate the TAPs
  // required and process them to completion or failure.  This is safe to call
  // when there's nothing to do.
  void trigger_resync();

  // Static entry point for TAP threads.  The argument must be a valid
  // TapBucketsThreadData object.  Returns the same object with the `success`
  // field updated appropriately.
  static void* tap_buckets_thread(void* data);

private:
  OutstandingWorkList scaling_worklist();
  void process_worklist(OutstandingWorkList& owl);
  TapList calculate_taps(const OutstandingWorkList& owl);
  bool perform_single_tap(const std::string& server,
                          const std::vector<uint16_t>& buckets,
                          pthread_t* handle);
  bool complete_single_tap(pthread_t thread_id,
                           std::string& tap_server);
  void blacklist_server(OutstandingWorkList& owl, const std::string& server);
  bool is_owl_valid(const OutstandingWorkList& owl);

  static uint16_t vbucket_for_key(const std::string& key);

  Updater<void, Astaire>* _updater;
  MemcachedStoreView* _view;
  MemcachedConfigReader* _view_cfg;
  Alarm* _alarm;
  AstaireGlobalStatistics* _global_stats;
  AstairePerConnectionStatistics* _per_conn_stats;
  std::string _self;
};

#endif
