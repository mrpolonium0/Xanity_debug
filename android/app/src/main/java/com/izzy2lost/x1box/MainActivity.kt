package com.izzy2lost.x1box

import android.content.Intent
import android.os.Bundle
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    val prefs = getSharedPreferences("x1box_prefs", MODE_PRIVATE)
    val setupComplete = prefs.getBoolean("setup_complete", false)
    val hasMcpx = prefs.getString("mcpxUri", null) != null
    val hasFlash = prefs.getString("flashUri", null) != null
    val hasHdd = prefs.getString("hddUri", null) != null

    if (!setupComplete || !hasMcpx || !hasFlash || !hasHdd) {
      startActivity(Intent(this, SetupWizardActivity::class.java))
      finish()
      return
    }

    super.onCreate(savedInstanceState)
  }

  override fun getLibraries(): Array<String> = arrayOf(
    "SDL2",
    "xemu",
  )
}
