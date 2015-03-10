#include "astaire_statistics.h"

#include <vector>
#include <string>

#include <iostream>

void AstaireGlobalStatistics::refreshed()
{
  std::vector<std::string> values;
  values.push_back(std::to_string(_total_buckets.load()));
  values.push_back(std::to_string(_resynced_bucket_count.load()));
  values.push_back(std::to_string(_resynced_keys_count.load()));
  values.push_back(std::to_string(_resynced_bytes_count.load()));
  values.push_back(std::to_string(_bandwidth));
  _statistic.report_change(values);
}

void AstaireGlobalStatistics::refresh(bool force)
{
  // Get the timestamp from the start of the current period, and the timestamp
  // now.
  uint_fast64_t timestamp_us = _timestamp_us.load();
  uint_fast64_t timestamp_us_now = get_timestamp_us();

  // If we're forced, or this period is already long enough, read the new
  // values and make the refreshed() callback.
  if ((timestamp_us_now >= timestamp_us + _target_period_us) &&
      (_timestamp_us.compare_exchange_weak(timestamp_us, timestamp_us_now)))
  {
    read(timestamp_us_now - timestamp_us);
    refreshed();
  }
  else if (force)
  {
    refreshed();
  }
}

void AstaireGlobalStatistics::read(uint_fast64_t period_us)
{
  uint_fast64_t period_s = period_us / (1000 * 1000);
  uint_fast64_t bandwidth_raw = _bandwidth_raw.exchange(0);
  if (period_s == 0)
  {
    _bandwidth = 0;
  }
  else
  {
    _bandwidth = bandwidth_raw / (period_s);
  }
}

void AstaireGlobalStatistics::reset()
{
  _timestamp_us.store(get_timestamp_us());

  // Use store(0) rather than zero_* so we don't call refresh till the end.
  _total_buckets.store(0);
  _resynced_bucket_count.store(0);
  _resynced_keys_count.store(0);
  _resynced_bytes_count.store(0);
  _bandwidth_raw.store(0);
  _bandwidth = 0;
  refresh(true);
}

void AstairePerConnectionStatistics::refreshed()
{
  std::vector<std::string> values;
  for (std::map<std::string, ConnectionRecord*>::iterator it = _connection_map.begin();
       it != _connection_map.end();
       ++it)
  {
    it->second->write_out(values);
  }
  _statistic.report_change(values);
}

void AstairePerConnectionStatistics::refresh(bool force)
{
  // Get the timestamp from the start of the current period, and the timestamp
  // now.
  uint_fast64_t timestamp_us = _timestamp_us.load();
  uint_fast64_t timestamp_us_now = get_timestamp_us();

  // If we're forced, or this period is already long enough, read the new
  // values and make the refreshed() callback.
  if ((timestamp_us_now >= timestamp_us + _target_period_us) &&
      (_timestamp_us.compare_exchange_weak(timestamp_us, timestamp_us_now)))
  {
    read(timestamp_us_now - timestamp_us);
    refreshed();
  }
  else if (force)
  {
    refreshed();
  }
}

void AstairePerConnectionStatistics::read(uint_fast64_t period_us)
{
  for (std::map<std::string, ConnectionRecord*>::iterator it = _connection_map.begin();
       it != _connection_map.end();
       ++it)
  {
    it->second->read(period_us);
  }
}

void AstairePerConnectionStatistics::reset()
{
  for (std::map<std::string, ConnectionRecord*>::iterator it = _connection_map.begin();
       it != _connection_map.end();
       ++it)
  {
    delete it->second;
  }
  _connection_map.clear();
  refresh(true);
}

AstairePerConnectionStatistics::ConnectionRecord*
AstairePerConnectionStatistics::add_connection(std::string server,
                                               std::vector<uint16_t> buckets)
{
  std::string address;
  int port;
  Utils::split_host_port(server, address, port);
  ConnectionRecord* conn_rec = new ConnectionRecord(this,
                                                    address,
                                                    port,
                                                    buckets,
                                                    &_lock,
                                                    _period_us);
  conn_rec->reset();
  conn_rec->set_total_buckets(buckets.size());
  _connection_map[server] = conn_rec;
  return conn_rec;
}

void AstairePerConnectionStatistics::ConnectionRecord::refreshed()
{
  // This is always handled by the owning AstairePerConnectionStatistic.
}

void AstairePerConnectionStatistics::ConnectionRecord::refresh(bool force)
{
  _parent->refresh(force);
}

void AstairePerConnectionStatistics::ConnectionRecord::read(uint_fast64_t period_us)
{
  for (std::map<uint16_t, BucketRecord*>::iterator it = _bucket_map.begin();
       it != _bucket_map.end();
       ++it)
  {
    it->second->read(period_us);
  }
}

void AstairePerConnectionStatistics::ConnectionRecord::reset()
{
  for (std::map<uint16_t, BucketRecord*>::iterator it = _bucket_map.begin();
       it != _bucket_map.end();
       ++it)
  {
    it->second->reset();
  }

  _total_buckets.store(0);
  _resynced_bucket_count.store(0);
}

void AstairePerConnectionStatistics::ConnectionRecord::write_out(std::vector<std::string>& vec)
{
  vec.push_back(address);
  vec.push_back(std::to_string(port));
  vec.push_back(std::to_string(_total_buckets.load()));
  vec.push_back(std::to_string(_resynced_bucket_count.load()));
  vec.push_back(std::to_string(_bucket_map.size()));
  for (std::map<uint16_t, BucketRecord*>::iterator it = _bucket_map.begin();
       it != _bucket_map.end();
       ++it)
  {
    it->second->write_out(vec);
  }
}

void AstairePerConnectionStatistics::BucketRecord::refreshed()
{
  // Always handled by the grandparent AstairePerConnectionStatistic.
}

void AstairePerConnectionStatistics::BucketRecord::refresh(bool force)
{
  _parent->refresh(force);
}

void AstairePerConnectionStatistics::BucketRecord::read(uint_fast64_t period_us)
{
  uint_fast64_t period_s = period_us / (1000 * 1000);
  uint_fast64_t bandwidth_raw = _bandwidth_raw.exchange(0);
  if (period_s == 0)
  {
    _bandwidth = 0;
  }
  else
  {
    _bandwidth = bandwidth_raw / (period_s);
  }
}

void AstairePerConnectionStatistics::BucketRecord::reset()
{
  _resynced_keys_count.store(0);
  _resynced_bytes_count.store(0);
  _bandwidth_raw.store(0);
  _bandwidth = 0;
}

void AstairePerConnectionStatistics::BucketRecord::write_out(std::vector<std::string>& vec)
{
  vec.push_back(std::to_string(bucket_id));
  vec.push_back(std::to_string(_resynced_keys_count.load()));
  vec.push_back(std::to_string(_resynced_bytes_count.load()));
  vec.push_back(std::to_string(_bandwidth));
}
