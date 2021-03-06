// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_LOCAL_ACCOUNT_POLICY_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_LOCAL_ACCOUNT_POLICY_SERVICE_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/extensions/device_local_account_external_policy_loader.h"
#include "chrome/browser/chromeos/policy/device_local_account_external_data_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "components/policy/core/common/cloud/cloud_policy_core.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"

namespace base {
class SequencedTaskRunner;
}

namespace chromeos {
class DeviceSettingsService;
class SessionManagerClient;
}

namespace net {
class URLRequestContextGetter;
}

namespace policy {

struct DeviceLocalAccount;
class DeviceLocalAccountExternalDataService;
class DeviceLocalAccountPolicyStore;
class DeviceManagementService;

// The main switching central that downloads, caches, refreshes, etc. policy for
// a single device-local account.
class DeviceLocalAccountPolicyBroker {
 public:
  // |task_runner| is the runner for policy refresh tasks.
  DeviceLocalAccountPolicyBroker(
      const DeviceLocalAccount& account,
      scoped_ptr<DeviceLocalAccountPolicyStore> store,
      scoped_refptr<DeviceLocalAccountExternalDataManager>
          external_data_manager,
      const scoped_refptr<base::SequencedTaskRunner>& task_runner);
  ~DeviceLocalAccountPolicyBroker();

  // Initialize the broker, loading its |store_|.
  void Initialize();

  // For the difference between |account_id| and |user_id|, see the
  // documentation of DeviceLocalAccount.
  const std::string& account_id() const { return account_id_; }
  const std::string& user_id() const { return user_id_; }

  scoped_refptr<chromeos::DeviceLocalAccountExternalPolicyLoader>
      extension_loader() const { return extension_loader_; }

  CloudPolicyCore* core() { return &core_; }
  const CloudPolicyCore* core() const { return &core_; }

  scoped_refptr<DeviceLocalAccountExternalDataManager> external_data_manager() {
    return external_data_manager_;
  }

  // Fire up the cloud connection for fetching policy for the account from the
  // cloud if this is an enterprise-managed device.
  void ConnectIfPossible(
      chromeos::DeviceSettingsService* device_settings_service,
      DeviceManagementService* device_management_service,
      scoped_refptr<net::URLRequestContextGetter> request_context);

  // Reads the refresh delay from policy and configures the refresh scheduler.
  void UpdateRefreshDelay();

  // Retrieves the display name for the account as stored in policy. Returns an
  // empty string if the policy is not present.
  std::string GetDisplayName() const;

 private:
  const std::string account_id_;
  const std::string user_id_;
  const scoped_ptr<DeviceLocalAccountPolicyStore> store_;
  scoped_refptr<DeviceLocalAccountExternalDataManager> external_data_manager_;
  scoped_refptr<chromeos::DeviceLocalAccountExternalPolicyLoader>
      extension_loader_;
  CloudPolicyCore core_;

  DISALLOW_COPY_AND_ASSIGN(DeviceLocalAccountPolicyBroker);
};

// Manages user policy blobs for device-local accounts present on the device.
// The actual policy blobs are brokered by session_manager (to prevent file
// manipulation), and we're making signature checks on the policy blobs to
// ensure they're issued by the device owner.
class DeviceLocalAccountPolicyService : public CloudPolicyStore::Observer {
 public:
  // Interface for interested parties to observe policy changes.
  class Observer {
   public:
    virtual ~Observer() {}

    // Policy for the given |user_id| has changed.
    virtual void OnPolicyUpdated(const std::string& user_id) = 0;

    // The list of accounts has been updated.
    virtual void OnDeviceLocalAccountsChanged() = 0;
  };

  DeviceLocalAccountPolicyService(
      chromeos::SessionManagerClient* session_manager_client,
      chromeos::DeviceSettingsService* device_settings_service,
      chromeos::CrosSettings* cros_settings,
      scoped_refptr<base::SequencedTaskRunner> store_background_task_runner,
      scoped_refptr<base::SequencedTaskRunner> extension_cache_task_runner,
      scoped_refptr<base::SequencedTaskRunner>
          external_data_service_backend_task_runner,
      scoped_refptr<base::SequencedTaskRunner> io_task_runner,
      scoped_refptr<net::URLRequestContextGetter> request_context);
  virtual ~DeviceLocalAccountPolicyService();

  // Shuts down the service and prevents further policy fetches from the cloud.
  void Shutdown();

  // Initializes the cloud policy service connection.
  void Connect(DeviceManagementService* device_management_service);

  // Get the policy broker for a given |user_id|. Returns NULL if that |user_id|
  // does not belong to an existing device-local account.
  DeviceLocalAccountPolicyBroker* GetBrokerForUser(const std::string& user_id);

  // Indicates whether policy has been successfully fetched for the given
  // |user_id|.
  bool IsPolicyAvailableForUser(const std::string& user_id);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // CloudPolicyStore::Observer:
  virtual void OnStoreLoaded(CloudPolicyStore* store) OVERRIDE;
  virtual void OnStoreError(CloudPolicyStore* store) OVERRIDE;

 private:
  typedef std::map<std::string, DeviceLocalAccountPolicyBroker*>
      PolicyBrokerMap;

  // Returns |true| if the directory in which force-installed extensions are
  // cached for |account_id| is busy, either because a broker that was using
  // this directory has not shut down completely yet or because the directory is
  // being deleted.
  bool IsExtensionCacheDirectoryBusy(const std::string& account_id);

  // Starts any extension caches that are not running yet but can be started now
  // because their cache directories are no longer busy.
  void StartExtensionCachesIfPossible();

  // Checks whether a broker exists for |account_id|. If so, starts the broker's
  // extension cache and returns |true|. Otherwise, returns |false|.
  bool StartExtensionCacheForAccountIfPresent(const std::string& account_id);

  // Called back when any extension caches belonging to device-local accounts
  // that no longer exist have been removed at start-up.
  void OnOrphanedExtensionCachesDeleted();

  // Called back when the extension cache for |account_id| has been shut down.
  void OnObsoleteExtensionCacheShutdown(const std::string& account_id);

  // Called back when the extension cache for |account_id| has been removed.
  void OnObsoleteExtensionCacheDeleted(const std::string& account_id);

  // Re-queries the list of defined device-local accounts from device settings
  // and updates |policy_brokers_| to match that list.
  void UpdateAccountList();

  // Calls |UpdateAccountList| if there are no previous calls pending.
  void UpdateAccountListIfNonePending();

  // Deletes brokers in |map| and clears it.
  void DeleteBrokers(PolicyBrokerMap* map);

  // Find the broker for a given |store|. Returns NULL if |store| is unknown.
  DeviceLocalAccountPolicyBroker* GetBrokerForStore(CloudPolicyStore* store);

  ObserverList<Observer, true> observers_;

  chromeos::SessionManagerClient* session_manager_client_;
  chromeos::DeviceSettingsService* device_settings_service_;
  chromeos::CrosSettings* cros_settings_;

  DeviceManagementService* device_management_service_;

  // The device-local account policy brokers, keyed by user ID.
  PolicyBrokerMap policy_brokers_;

  // Whether a call to UpdateAccountList() is pending because |cros_settings_|
  // are not trusted yet.
  bool waiting_for_cros_settings_;

  // Orphaned extension caches are removed at startup. This tracks the status of
  // that process.
  enum OrphanCacheDeletionState {
    NOT_STARTED,
    IN_PROGRESS,
    DONE,
  };
  OrphanCacheDeletionState orphan_cache_deletion_state_;

  // Account IDs whose extension cache directories are busy, either because a
  // broker for the account has not shut down completely yet or because the
  // directory is being deleted.
  std::set<std::string> busy_extension_cache_directories_;

  const scoped_refptr<base::SequencedTaskRunner> store_background_task_runner_;
  const scoped_refptr<base::SequencedTaskRunner> extension_cache_task_runner_;

  scoped_ptr<DeviceLocalAccountExternalDataService> external_data_service_;

  scoped_refptr<net::URLRequestContextGetter> request_context_;

  const scoped_ptr<chromeos::CrosSettings::ObserverSubscription>
      local_accounts_subscription_;

  base::WeakPtrFactory<DeviceLocalAccountPolicyService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceLocalAccountPolicyService);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_LOCAL_ACCOUNT_POLICY_SERVICE_H_
