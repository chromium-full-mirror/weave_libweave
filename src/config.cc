// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/config.h"

#include <algorithm>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/guid.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <weave/enum_to_string.h>

#include "src/privet/privet_types.h"
#include "src/string_utils.h"

namespace weave {

namespace config_keys {

const char kVersion[] = "version";

const char kClientId[] = "client_id";
const char kClientSecret[] = "client_secret";
const char kApiKey[] = "api_key";
const char kOAuthURL[] = "oauth_url";
const char kServiceURL[] = "service_url";
const char kName[] = "name";
const char kDescription[] = "description";
const char kLocation[] = "location";
const char kLocalAnonymousAccessRole[] = "local_anonymous_access_role";
const char kLocalDiscoveryEnabled[] = "local_discovery_enabled";
const char kLocalPairingEnabled[] = "local_pairing_enabled";
const char kRefreshToken[] = "refresh_token";
const char kCloudId[] = "cloud_id";
const char kDeviceId[] = "device_id";
const char kRobotAccount[] = "robot_account";
const char kLastConfiguredSsid[] = "last_configured_ssid";
const char kSecret[] = "secret";

}  // namespace config_keys

const char kWeaveUrl[] = "https://www.googleapis.com/weave/v1/";
const char kDeprecatedUrl[] = "https://www.googleapis.com/clouddevices/v1/";

namespace {

const int kCurrentConfigVersion = 1;

void MigrateFromV0(base::Value* dict) {
  std::string* cloud_id = dict->FindStringKey(config_keys::kCloudId);
  if (cloud_id && !cloud_id->empty())
    return;
  base::Optional<base::Value> tmp = dict->ExtractKey(config_keys::kDeviceId);
  if (tmp)
    dict->SetKey(config_keys::kCloudId, std::move(*tmp));
}

Config::Settings CreateDefaultSettings() {
  Config::Settings result;
  result.oauth_url = "https://accounts.google.com/o/oauth2/";
  result.service_url = kWeaveUrl;
  result.local_anonymous_access_role = AuthScope::kViewer;
  result.pairing_modes.emplace(PairingType::kPinCode);
  result.device_id = base::GenerateGUID();
  return result;
}

}  // namespace

Config::Config(provider::ConfigStore* config_store)
    : settings_{CreateDefaultSettings()}, config_store_{config_store} {
}

void Config::AddOnChangedCallback(const OnChangedCallback& callback) {
  on_changed_.push_back(callback);
  // Force to read current state.
  callback.Run(settings_);
}

const Config::Settings& Config::GetSettings() const {
  return settings_;
}

void Config::Load() {
  Transaction change{this};
  change.save_ = false;

  settings_ = CreateDefaultSettings();

  if (!config_store_)
    return;

  // Crash on any mistakes in defaults.
  CHECK(config_store_->LoadDefaults(&settings_));

  CHECK(!settings_.client_id.empty());
  CHECK(!settings_.client_secret.empty());
  CHECK(!settings_.api_key.empty());
  CHECK(!settings_.oauth_url.empty());
  CHECK(!settings_.service_url.empty());
  CHECK(!settings_.oem_name.empty());
  CHECK(!settings_.model_name.empty());
  CHECK(!settings_.model_id.empty());
  CHECK(!settings_.name.empty());
  CHECK(!settings_.device_id.empty());
  CHECK_EQ(
      settings_.embedded_code.empty(),
      std::find(settings_.pairing_modes.begin(), settings_.pairing_modes.end(),
                PairingType::kEmbeddedCode) == settings_.pairing_modes.end());

  // Values below will be generated at runtime.
  CHECK(settings_.cloud_id.empty());
  CHECK(settings_.refresh_token.empty());
  CHECK(settings_.robot_account.empty());
  CHECK(settings_.last_configured_ssid.empty());
  CHECK(settings_.secret.empty());

  change.LoadState();
}

void Config::Transaction::LoadState() {
  if (!config_->config_store_)
    return;
  std::string json_string = config_->config_store_->LoadSettings();
  if (json_string.empty())
    return;

  auto value = base::JSONReader::Read(json_string);
  if (!value || !value->is_dict()) {
    LOG(ERROR) << "Failed to parse settings.";
    return;
  }

  int loaded_version = value->FindIntKey(config_keys::kVersion).value_or(0);

  if (loaded_version != kCurrentConfigVersion) {
    LOG(INFO) << "State version mismatch. expected: " << kCurrentConfigVersion
              << ", loaded: " << loaded_version;
    save_ = true;
  }

  if (loaded_version == 0)
    MigrateFromV0(&value.value());

  if (const std::string* tmp = value->FindStringKey(config_keys::kClientId))
    set_client_id(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kClientSecret))
    set_client_secret(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kApiKey))
    set_api_key(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kOAuthURL))
    set_oauth_url(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kServiceURL))
    set_service_url(*tmp == kDeprecatedUrl ? kWeaveUrl : *tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kName))
    set_name(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kDescription))
    set_description(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kLocation))
    set_location(*tmp);

  if (const std::string* tmp =
          value->FindStringKey(config_keys::kLocalAnonymousAccessRole)) {
    AuthScope scope = AuthScope::kNone;
    if (StringToEnum(*tmp, &scope))
      set_local_anonymous_access_role(scope);
  }

  if (auto tmp = value->FindBoolKey(config_keys::kLocalDiscoveryEnabled))
    set_local_discovery_enabled(*tmp);

  if (auto tmp = value->FindBoolKey(config_keys::kLocalPairingEnabled))
    set_local_pairing_enabled(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kCloudId))
    set_cloud_id(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kDeviceId))
    set_device_id(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kRefreshToken))
    set_refresh_token(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kRobotAccount))
    set_robot_account(*tmp);

  if (const std::string* tmp =
          value->FindStringKey(config_keys::kLastConfiguredSsid))
    set_last_configured_ssid(*tmp);

  if (const std::string* tmp = value->FindStringKey(config_keys::kSecret))
    set_secret(*tmp);
}

void Config::Save() {
  if (!config_store_)
    return;

  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetIntKey(config_keys::kVersion, kCurrentConfigVersion);

  dict.SetStringKey(config_keys::kClientId, settings_.client_id);
  dict.SetStringKey(config_keys::kClientSecret, settings_.client_secret);
  dict.SetStringKey(config_keys::kApiKey, settings_.api_key);
  dict.SetStringKey(config_keys::kOAuthURL, settings_.oauth_url);
  dict.SetStringKey(config_keys::kServiceURL, settings_.service_url);
  dict.SetStringKey(config_keys::kRefreshToken, settings_.refresh_token);
  dict.SetStringKey(config_keys::kCloudId, settings_.cloud_id);
  dict.SetStringKey(config_keys::kDeviceId, settings_.device_id);
  dict.SetStringKey(config_keys::kRobotAccount, settings_.robot_account);
  dict.SetStringKey(config_keys::kLastConfiguredSsid,
                    settings_.last_configured_ssid);
  dict.SetStringKey(config_keys::kSecret, settings_.secret);
  dict.SetStringKey(config_keys::kName, settings_.name);
  dict.SetStringKey(config_keys::kDescription, settings_.description);
  dict.SetStringKey(config_keys::kLocation, settings_.location);
  dict.SetStringKey(config_keys::kLocalAnonymousAccessRole,
                    EnumToString(settings_.local_anonymous_access_role));
  dict.SetBoolKey(config_keys::kLocalDiscoveryEnabled,
                  settings_.local_discovery_enabled);
  dict.SetBoolKey(config_keys::kLocalPairingEnabled,
                  settings_.local_pairing_enabled);

  std::string json_string;
  base::JSONWriter::WriteWithOptions(
      dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string);

  config_store_->SaveSettings(json_string);
}

Config::Transaction::~Transaction() {
  Commit();
}

void Config::Transaction::Commit() {
  if (!config_)
    return;
  if (save_)
    config_->Save();
  for (const auto& cb : config_->on_changed_)
    cb.Run(*settings_);
  config_ = nullptr;
}

}  // namespace weave
