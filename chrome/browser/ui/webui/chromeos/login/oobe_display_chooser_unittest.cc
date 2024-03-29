// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/oobe_display_chooser.h"

#include <memory>
#include <vector>

#include "ash/display/display_configuration_controller.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/ws/public/cpp/input_devices/input_device_client_test_api.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/display.h"
#include "ui/display/display_observer.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/manager/test/touch_device_manager_test_api.h"
#include "ui/display/screen.h"
#include "ui/display/test/display_manager_test_api.h"
#include "ui/events/devices/touchscreen_device.h"

namespace chromeos {

namespace {

class TestCrosDisplayConfig : public ash::mojom::CrosDisplayConfigController {
 public:
  TestCrosDisplayConfig() : binding_(this) {}

  ash::mojom::CrosDisplayConfigControllerPtr CreateInterfacePtrAndBind() {
    ash::mojom::CrosDisplayConfigControllerPtr ptr;
    binding_.Bind(mojo::MakeRequest(&ptr));
    return ptr;
  }

  // ash::mojom::CrosDisplayConfigController:
  void AddObserver(ash::mojom::CrosDisplayConfigObserverAssociatedPtrInfo
                       observer) override {}
  void GetDisplayLayoutInfo(GetDisplayLayoutInfoCallback callback) override {}
  void SetDisplayLayoutInfo(ash::mojom::DisplayLayoutInfoPtr info,
                            SetDisplayLayoutInfoCallback callback) override {}
  void GetDisplayUnitInfoList(
      bool single_unified,
      GetDisplayUnitInfoListCallback callback) override {}
  void SetDisplayProperties(const std::string& id,
                            ash::mojom::DisplayConfigPropertiesPtr properties,
                            SetDisplayPropertiesCallback callback) override {
    if (properties->set_primary) {
      int64_t display_id;
      base::StringToInt64(id, &display_id);
      ash::Shell::Get()->window_tree_host_manager()->SetPrimaryDisplayId(
          display_id);
    }
    std::move(callback).Run(ash::mojom::DisplayConfigResult::kSuccess);
  }
  void SetUnifiedDesktopEnabled(bool enabled) override {}
  void OverscanCalibration(const std::string& display_id,
                           ash::mojom::DisplayConfigOperation op,
                           const base::Optional<gfx::Insets>& delta,
                           OverscanCalibrationCallback callback) override {}
  void TouchCalibration(const std::string& display_id,
                        ash::mojom::DisplayConfigOperation op,
                        ash::mojom::TouchCalibrationPtr calibration,
                        TouchCalibrationCallback callback) override {}

 private:
  mojo::Binding<ash::mojom::CrosDisplayConfigController> binding_;

  DISALLOW_COPY_AND_ASSIGN(TestCrosDisplayConfig);
};

class OobeDisplayChooserTest : public ash::AshTestBase {
 public:
  OobeDisplayChooserTest() : ash::AshTestBase() {}

  int64_t GetPrimaryDisplay() {
    return display::Screen::GetScreen()->GetPrimaryDisplay().id();
  }

  // ash::AshTestBase:
  void SetUp() override {
    ash::AshTestBase::SetUp();

    cros_display_config_ = std::make_unique<TestCrosDisplayConfig>();
    display_chooser_ = std::make_unique<OobeDisplayChooser>();
    display_chooser_->set_cros_display_config_ptr_for_test(
        cros_display_config_->CreateInterfacePtrAndBind());

    ui::InputDeviceClientTestApi().OnDeviceListsComplete();
  }

  OobeDisplayChooser* display_chooser() { return display_chooser_.get(); }

 private:
  std::unique_ptr<TestCrosDisplayConfig> cros_display_config_;
  std::unique_ptr<OobeDisplayChooser> display_chooser_;

  DISALLOW_COPY_AND_ASSIGN(OobeDisplayChooserTest);
};

const uint16_t kWhitelistedId = 0x266e;

}  // namespace

TEST_F(OobeDisplayChooserTest, PreferTouchAsPrimary) {
  // Setup 2 displays, second one is intended to be a touch display
  std::vector<display::ManagedDisplayInfo> display_info;
  display_info.push_back(
      display::ManagedDisplayInfo::CreateFromSpecWithID("0+0-3000x2000", 1));
  display_info.push_back(
      display::ManagedDisplayInfo::CreateFromSpecWithID("3000+0-800x600", 2));
  display_manager()->OnNativeDisplaysChanged(display_info);
  base::RunLoop().RunUntilIdle();

  // Make sure the non-touch display is primary
  ash::Shell::Get()->window_tree_host_manager()->SetPrimaryDisplayId(1);

  // Setup corresponding TouchscreenDevice object
  ui::TouchscreenDevice touchscreen =
      ui::TouchscreenDevice(1, ui::InputDeviceType::INPUT_DEVICE_EXTERNAL,
                            "Touchscreen", gfx::Size(800, 600), 1);
  touchscreen.vendor_id = kWhitelistedId;
  ui::InputDeviceClientTestApi().SetTouchscreenDevices({touchscreen});
  base::RunLoop().RunUntilIdle();

  // Associate touchscreen device with display
  display::test::TouchDeviceManagerTestApi(
      display_manager()->touch_device_manager())
      .Associate(&display_info[1], touchscreen);
  display_manager()->OnNativeDisplaysChanged(display_info);
  base::RunLoop().RunUntilIdle();

  // For mus we have to explicitly tell the InputDeviceClient the
  // TouchscreenDevices. Normally InputDeviceClient is told of the
  // TouchscreenDevices by way of implementing
  // ws::mojom::InputDeviceObserverMojo. In unit tests InputDeviceClient is not
  // wired to the window server (the window server isn't running).
  touchscreen.target_display_id = display_info[1].id();
  ui::InputDeviceClientTestApi().SetTouchscreenDevices({touchscreen}, true);

  EXPECT_EQ(1, GetPrimaryDisplay());
  display_chooser()->TryToPlaceUiOnTouchDisplay();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2, GetPrimaryDisplay());
}

TEST_F(OobeDisplayChooserTest, DontSwitchFromTouch) {
  // Setup 2 displays, second one is intended to be a touch display
  std::vector<display::ManagedDisplayInfo> display_info;
  display_info.push_back(
      display::ManagedDisplayInfo::CreateFromSpecWithID("0+0-3000x2000", 1));
  display_info.push_back(
      display::ManagedDisplayInfo::CreateFromSpecWithID("3000+0-800x600", 2));
  display_info[0].set_touch_support(display::Display::TouchSupport::AVAILABLE);
  display_manager()->OnNativeDisplaysChanged(display_info);
  base::RunLoop().RunUntilIdle();

  // Make sure the non-touch display is primary
  ash::Shell::Get()->window_tree_host_manager()->SetPrimaryDisplayId(1);

  // Setup corresponding TouchscreenDevice object
  ui::TouchscreenDevice touchscreen =
      ui::TouchscreenDevice(1, ui::InputDeviceType::INPUT_DEVICE_EXTERNAL,
                            "Touchscreen", gfx::Size(800, 600), 1);
  touchscreen.vendor_id = kWhitelistedId;
  ui::InputDeviceClientTestApi().SetTouchscreenDevices({touchscreen});
  base::RunLoop().RunUntilIdle();

  // Associate touchscreen device with display
  display::test::TouchDeviceManagerTestApi(
      display_manager()->touch_device_manager())
      .Associate(&display_info[1], touchscreen);
  display_manager()->OnNativeDisplaysChanged(display_info);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, GetPrimaryDisplay());
  display_chooser()->TryToPlaceUiOnTouchDisplay();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, GetPrimaryDisplay());
}

}  // namespace chromeos
