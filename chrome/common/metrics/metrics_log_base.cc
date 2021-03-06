// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/metrics/metrics_log_base.h"

#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_samples.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/metrics/proto/histogram_event.pb.h"
#include "chrome/common/metrics/proto/system_profile.pb.h"
#include "chrome/common/metrics/proto/user_action_event.pb.h"
#include "components/metrics/metrics_hashes.h"

using base::Histogram;
using base::HistogramBase;
using base::HistogramSamples;
using base::SampleCountIterator;
using base::Time;
using base::TimeDelta;
using metrics::HistogramEventProto;
using metrics::SystemProfileProto;
using metrics::UserActionEventProto;

namespace {

// Any id less than 16 bytes is considered to be a testing id.
bool IsTestingID(const std::string& id) {
  return id.size() < 16;
}

SystemProfileProto::Channel AsProtobufChannel(
    chrome::VersionInfo::Channel channel) {
  switch (channel) {
    case chrome::VersionInfo::CHANNEL_UNKNOWN:
      return SystemProfileProto::CHANNEL_UNKNOWN;
    case chrome::VersionInfo::CHANNEL_CANARY:
      return SystemProfileProto::CHANNEL_CANARY;
    case chrome::VersionInfo::CHANNEL_DEV:
      return SystemProfileProto::CHANNEL_DEV;
    case chrome::VersionInfo::CHANNEL_BETA:
      return SystemProfileProto::CHANNEL_BETA;
    case chrome::VersionInfo::CHANNEL_STABLE:
      return SystemProfileProto::CHANNEL_STABLE;
    default:
      NOTREACHED();
      return SystemProfileProto::CHANNEL_UNKNOWN;
  }
}

}  // namespace

MetricsLogBase::MetricsLogBase(const std::string& client_id, int session_id,
                               const std::string& version_string)
    : num_events_(0),
      locked_(false) {
  if (IsTestingID(client_id))
    uma_proto_.set_client_id(0);
  else
    uma_proto_.set_client_id(Hash(client_id));

  uma_proto_.set_session_id(session_id);
  uma_proto_.mutable_system_profile()->set_build_timestamp(GetBuildTime());
  uma_proto_.mutable_system_profile()->set_app_version(version_string);
  uma_proto_.mutable_system_profile()->set_channel(
      AsProtobufChannel(chrome::VersionInfo::GetChannel()));
}

MetricsLogBase::~MetricsLogBase() {}

// static
uint64 MetricsLogBase::Hash(const std::string& value) {
  uint64 hash = metrics::HashMetricName(value);

  // The following log is VERY helpful when folks add some named histogram into
  // the code, but forgot to update the descriptive list of histograms.  When
  // that happens, all we get to see (server side) is a hash of the histogram
  // name.  We can then use this logging to find out what histogram name was
  // being hashed to a given MD5 value by just running the version of Chromium
  // in question with --enable-logging.
  DVLOG(1) << "Metrics: Hash numeric [" << value << "]=[" << hash << "]";

  return hash;
}

// static
int64 MetricsLogBase::GetBuildTime() {
  static int64 integral_build_time = 0;
  if (!integral_build_time) {
    Time time;
    const char* kDateTime = __DATE__ " " __TIME__ " GMT";
    bool result = Time::FromString(kDateTime, &time);
    DCHECK(result);
    integral_build_time = static_cast<int64>(time.ToTimeT());
  }
  return integral_build_time;
}

// static
int64 MetricsLogBase::GetCurrentTime() {
  return (base::TimeTicks::Now() - base::TimeTicks()).InSeconds();
}

void MetricsLogBase::CloseLog() {
  DCHECK(!locked_);
  locked_ = true;
}

void MetricsLogBase::GetEncodedLog(std::string* encoded_log) {
  DCHECK(locked_);
  uma_proto_.SerializeToString(encoded_log);
}

void MetricsLogBase::RecordUserAction(const char* key) {
  DCHECK(!locked_);

  UserActionEventProto* user_action = uma_proto_.add_user_action_event();
  user_action->set_name_hash(Hash(key));
  user_action->set_time(GetCurrentTime());

  ++num_events_;
}

void MetricsLogBase::RecordHistogramDelta(const std::string& histogram_name,
                                          const HistogramSamples& snapshot) {
  DCHECK(!locked_);
  DCHECK_NE(0, snapshot.TotalCount());

  // We will ignore the MAX_INT/infinite value in the last element of range[].

  HistogramEventProto* histogram_proto = uma_proto_.add_histogram_event();
  histogram_proto->set_name_hash(Hash(histogram_name));
  histogram_proto->set_sum(snapshot.sum());

  for (scoped_ptr<SampleCountIterator> it = snapshot.Iterator();
       !it->Done();
       it->Next()) {
    HistogramBase::Sample min;
    HistogramBase::Sample max;
    HistogramBase::Count count;
    it->Get(&min, &max, &count);
    HistogramEventProto::Bucket* bucket = histogram_proto->add_bucket();
    bucket->set_min(min);
    bucket->set_max(max);
    bucket->set_count(count);
  }

  // Omit fields to save space (see rules in histogram_event.proto comments).
  for (int i = 0; i < histogram_proto->bucket_size(); ++i) {
    HistogramEventProto::Bucket* bucket = histogram_proto->mutable_bucket(i);
    if (i + 1 < histogram_proto->bucket_size() &&
        bucket->max() == histogram_proto->bucket(i + 1).min()) {
      bucket->clear_max();
    } else if (bucket->max() == bucket->min() + 1) {
      bucket->clear_min();
    }
  }
}
