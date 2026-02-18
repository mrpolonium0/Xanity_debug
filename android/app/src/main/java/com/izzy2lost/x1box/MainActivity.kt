package com.izzy2lost.x1box

import android.content.Context
import android.content.Intent
import android.hardware.input.InputManager
import android.os.Build
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.FrameLayout
import androidx.appcompat.app.AlertDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity(), InputManager.InputDeviceListener {
  private var onScreenController: OnScreenController? = null
  private var controllerBridge: ControllerInputBridge? = null
  private var isControllerVisible = false
  private var inputManager: InputManager? = null
  private var hasPhysicalController = false
  private var inGameMenuDialog: AlertDialog? = null
  private var startButtonDown = false
  private var selectButtonDown = false
  private var comboTriggered = false

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setupOnScreenController()
    setupControllerDetection()
    hideSystemUI()
  }

  override fun onWindowFocusChanged(hasFocus: Boolean) {
    super.onWindowFocusChanged(hasFocus)
    if (hasFocus) {
      hideSystemUI()
    }
  }

  override fun onBackPressed() {
    val currentDialog = inGameMenuDialog
    if (currentDialog?.isShowing == true) {
      currentDialog.dismiss()
      return
    }
    showInGameMenu()
  }

  override fun dispatchKeyEvent(event: KeyEvent): Boolean {
    if (event.keyCode == KeyEvent.KEYCODE_BACK && !isGamepadKeyEvent(event)) {
      if (event.action == KeyEvent.ACTION_UP && event.repeatCount == 0) {
        val currentDialog = inGameMenuDialog
        if (currentDialog?.isShowing == true) {
          currentDialog.dismiss()
        } else {
          showInGameMenu()
        }
      }
      return true
    }

    if (handleGamepadMenuCombo(event)) {
      return true
    }
    return super.dispatchKeyEvent(event)
  }

  private fun hideSystemUI() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      // Android 11 (API 30) and above
      window.setDecorFitsSystemWindows(false)
      window.insetsController?.let { controller ->
        controller.hide(WindowInsets.Type.systemBars())
        controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
      }
    } else {
      // Android 10 and below
      @Suppress("DEPRECATION")
      window.decorView.systemUiVisibility = (
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        or View.SYSTEM_UI_FLAG_FULLSCREEN
        or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
      )
    }
  }

  private fun setupOnScreenController() {
    // Create on-screen controller
    onScreenController = OnScreenController(this).apply {
      layoutParams = FrameLayout.LayoutParams(
        FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT
      )
    }

    // Create input bridge
    controllerBridge = ControllerInputBridge()
    onScreenController?.setControllerListener(controllerBridge!!)

    // Add to layout
    mLayout?.addView(onScreenController)

    // Check for existing controllers and show/hide accordingly
    updateControllerVisibility()
  }

  override fun onResume() {
    super.onResume()
    
    // Register virtual controller after SDL is initialized
    // Use a delay to ensure SDL is fully ready
    mLayout?.postDelayed({
      registerVirtualController()
    }, 1000)
  }

  private fun registerVirtualController() {
    try {
      // Register the virtual on-screen controller as a joystick device
      // Device ID: -2, Name: "On-Screen Controller"
      org.libsdl.app.SDLControllerManager.nativeAddJoystick(
        -2, // device_id
        "On-Screen Controller", // name
        "Virtual touchscreen controller", // desc
        0x045e, // vendor_id (Microsoft)
        0x028e, // product_id (Xbox 360 Controller)
        false, // is_accelerometer
        0xFFFF, // button_mask (all buttons)
        6, // naxes (left X/Y, right X/Y, left trigger, right trigger)
        0x3F, // axis_mask (6 axes)
        0, // nhats
        0  // nballs
      )
      android.util.Log.d("MainActivity", "Virtual controller registered successfully")
    } catch (e: Exception) {
      android.util.Log.e("MainActivity", "Failed to register virtual controller: ${e.message}")
    }
  }

  private fun setupControllerDetection() {
    inputManager = getSystemService(Context.INPUT_SERVICE) as InputManager
    inputManager?.registerInputDeviceListener(this, null)
    
    // Check for already connected controllers
    checkForPhysicalControllers()
  }

  private fun checkForPhysicalControllers() {
    val deviceIds = inputManager?.inputDeviceIds ?: return
    hasPhysicalController = deviceIds.any { deviceId ->
      val device = inputManager?.getInputDevice(deviceId)
      isGameController(device)
    }
    updateControllerVisibility()
  }

  private fun isGameController(device: InputDevice?): Boolean {
    if (device == null) return false
    
    val sources = device.sources
    
    // Check if device is a gamepad or joystick
    return ((sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
           ((sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
  }

  private fun updateControllerVisibility() {
    // Show on-screen controller only if no physical controller is connected
    val shouldShow = !hasPhysicalController
    
    if (shouldShow != isControllerVisible) {
      isControllerVisible = shouldShow
      onScreenController?.visibility = if (shouldShow) View.VISIBLE else View.GONE
    }
  }

  // InputDeviceListener callbacks
  override fun onInputDeviceAdded(deviceId: Int) {
    val device = inputManager?.getInputDevice(deviceId)
    if (isGameController(device)) {
      hasPhysicalController = true
      updateControllerVisibility()
    }
  }

  override fun onInputDeviceRemoved(deviceId: Int) {
    // Recheck all devices to see if any controllers remain
    checkForPhysicalControllers()
  }

  override fun onInputDeviceChanged(deviceId: Int) {
    // Recheck all devices in case configuration changed
    checkForPhysicalControllers()
  }

  override fun onDestroy() {
    inGameMenuDialog?.dismiss()
    inGameMenuDialog = null

    // Unregister virtual controller
    try {
      org.libsdl.app.SDLControllerManager.nativeRemoveJoystick(-2)
    } catch (e: Exception) {
      android.util.Log.e("MainActivity", "Failed to unregister virtual controller: ${e.message}")
    }
    
    inputManager?.unregisterInputDeviceListener(this)
    super.onDestroy()
  }

  // Manual control methods (for settings/preferences)
  fun toggleOnScreenController() {
    isControllerVisible = !isControllerVisible
    onScreenController?.visibility = if (isControllerVisible) View.VISIBLE else View.GONE
  }

  fun showOnScreenController() {
    isControllerVisible = true
    onScreenController?.visibility = View.VISIBLE
  }

  fun hideOnScreenController() {
    isControllerVisible = false
    onScreenController?.visibility = View.GONE
  }

  fun forceUpdateControllerVisibility() {
    checkForPhysicalControllers()
  }

  private fun handleGamepadMenuCombo(event: KeyEvent): Boolean {
    if (!isGamepadKeyEvent(event)) {
      return false
    }

    val isStartKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_START
    val isSelectKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
      event.keyCode == KeyEvent.KEYCODE_BACK
    if (!isStartKey && !isSelectKey) {
      return false
    }

    when (event.action) {
      KeyEvent.ACTION_DOWN -> {
        if (isStartKey) {
          startButtonDown = true
        }
        if (isSelectKey) {
          selectButtonDown = true
        }

        if (!comboTriggered && event.repeatCount == 0 &&
          startButtonDown && selectButtonDown) {
          comboTriggered = true
          showInGameMenu()
          return true
        }
      }
      KeyEvent.ACTION_UP -> {
        if (isStartKey) {
          startButtonDown = false
        }
        if (isSelectKey) {
          selectButtonDown = false
        }
        if (!startButtonDown || !selectButtonDown) {
          comboTriggered = false
        }
      }
    }

    return comboTriggered
  }

  private fun isGamepadKeyEvent(event: KeyEvent): Boolean {
    val source = event.source
    return ((source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
      ((source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
  }

  private fun showInGameMenu() {
    val options = arrayOf(
      getString(R.string.in_game_menu_resume),
      if (isControllerVisible) {
        getString(R.string.in_game_menu_hide_touch_controls)
      } else {
        getString(R.string.in_game_menu_show_touch_controls)
      },
      getString(R.string.in_game_menu_exit_to_library),
      getString(R.string.in_game_menu_quit_app),
    )

    val dialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xemu_RoundedDialog)
      .setTitle(getString(R.string.in_game_menu_title))
      .setItems(options) { _, which ->
        when (which) {
          0 -> {
            // Resume
          }
          1 -> toggleOnScreenController()
          2 -> exitToGameLibrary()
          3 -> finishAffinity()
        }
      }
      .setOnDismissListener {
        inGameMenuDialog = null
        hideSystemUI()
      }
      .create()

    inGameMenuDialog = dialog
    dialog.show()
  }

  private fun exitToGameLibrary() {
    val intent = Intent(this, GameLibraryActivity::class.java).apply {
      addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
    }
    startActivity(intent)
    finish()
  }

  override fun getLibraries(): Array<String> = arrayOf(
    "SDL2",
    "xemu",
  )
}
