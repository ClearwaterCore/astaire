/**
 * @file astaire_statistics.cpp - Astaire statistics
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "astaire_statistics.hpp"

#include <vector>
#include <string>

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

  // If enough time has passed, read the new values and make the refreshed()
  // callback.  If we're being forced, call refreshed() anyway.
  //
  // If we fail the CAS, someone else has just handled this tick and we can
  // leave the reporting to them.
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

void AstaireGlobalStatistics::thread_func()
{
  pthread_mutex_lock(&_refresh_mutex);
  while (!_terminated)
  {
    struct timespec next_refresh;
    clock_gettime(CLOCK_MONOTONIC, &next_refresh);
    next_refresh.tv_sec += 1;
    pthread_cond_timedwait(&_refresh_cond, &_refresh_mutex, &next_refresh);
    refresh(true);
  }
  pthread_mutex_unlock(&_refresh_mutex);
}

void AstairePerConnectionStatistics::refreshed()
{
  std::vector<std::string> values;
  for (std::vector<ConnectionRecord*>::iterator it = _connections.begin();
       it != _connections.end();
       ++it)
  {
    (*it)->write_out(values);
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
  for (std::vector<ConnectionRecord*>::iterator it = _connections.begin();
       it != _connections.end();
       ++it)
  {
    (*it)->read(period_us);
  }
}

void AstairePerConnectionStatistics::reset()
{
  _timestamp_us.store(get_timestamp_us());
  for (std::vector<ConnectionRecord*>::iterator it = _connections.begin();
       it != _connections.end();
       ++it)
  {
    delete (*it);
  }
  _connections.clear();
  refresh(true);
}

AstairePerConnectionStatistics::ConnectionRecord*
AstairePerConnectionStatistics::add_connection(std::string server,
                                               std::vector<uint16_t> buckets)
{
  std::string address;
  int port;
  if (!Utils::split_host_port(server, address, port))
  {
    // Just use the server as the address.
    address = server;
    port = 0;
  }

  ConnectionRecord* conn_rec = new ConnectionRecord(this,
                                                    address,
                                                    port,
                                                    buckets,
                                                    &_lock,
                                                    _period_us);
  _connections.push_back(conn_rec);
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
