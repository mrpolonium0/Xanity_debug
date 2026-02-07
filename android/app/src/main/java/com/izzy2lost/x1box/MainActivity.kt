package com.izzy2lost.x1box

import android.os.Bundle
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
  }

  override fun getLibraries(): Array<String> = arrayOf(
    "SDL2",
    "xemu",
  )
}
