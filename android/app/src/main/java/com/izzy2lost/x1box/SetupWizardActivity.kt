package com.izzy2lost.x1box

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton

class SetupWizardActivity : AppCompatActivity() {
  private val prefs by lazy { getSharedPreferences("x1box_prefs", MODE_PRIVATE) }

  private lateinit var pageMcpx: View
  private lateinit var pageFlash: View
  private lateinit var pageHdd: View
  private lateinit var mcpxPathText: TextView
  private lateinit var flashPathText: TextView
  private lateinit var hddPathText: TextView
  private lateinit var btnBack: MaterialButton
  private lateinit var btnNext: MaterialButton
  private lateinit var indicatorMcpx: View
  private lateinit var indicatorFlash: View
  private lateinit var indicatorHdd: View

  private var mcpxUri: Uri? = null
  private var flashUri: Uri? = null
  private var hddUri: Uri? = null
  private var currentStep = 0

  private val mcpxExts = setOf("bin", "rom", "img")
  private val flashExts = setOf("bin", "rom", "img")
  private val hddExts = setOf("qcow2", "img")

  private val pickMcpx =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
      if (uri != null) {
        if (!isAllowedExtension(uri, mcpxExts)) {
          showExtensionError(mcpxExts)
          return@registerForActivityResult
        }
        persistUriPermission(uri)
        mcpxUri = uri
        prefs.edit().putString("mcpxUri", uri.toString()).apply()
        updateMcpxSelection()
        updateButtons()
      }
    }

  private val pickFlash =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
      if (uri != null) {
        if (!isAllowedExtension(uri, flashExts)) {
          showExtensionError(flashExts)
          return@registerForActivityResult
        }
        persistUriPermission(uri)
        flashUri = uri
        prefs.edit().putString("flashUri", uri.toString()).apply()
        updateFlashSelection()
        updateButtons()
      }
    }

  private val pickHdd =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
      if (uri != null) {
        if (!isAllowedExtension(uri, hddExts)) {
          showExtensionError(hddExts)
          return@registerForActivityResult
        }
        persistUriPermission(uri)
        hddUri = uri
        prefs.edit().putString("hddUri", uri.toString()).apply()
        updateHddSelection()
        updateButtons()
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    mcpxUri = prefs.getString("mcpxUri", null)?.let(Uri::parse)
    flashUri = prefs.getString("flashUri", null)?.let(Uri::parse)
    hddUri = prefs.getString("hddUri", null)?.let(Uri::parse)

    if (prefs.getBoolean("setup_complete", false) && mcpxUri != null && flashUri != null && hddUri != null) {
      goToMain()
      return
    }

    setContentView(R.layout.activity_setup_wizard)

    val setupRoot: View = findViewById(R.id.setup_root)
    val setupCard: View = findViewById(R.id.setup_card)
    setupRoot.post {
      val target = (setupRoot.height * 0.92f).toInt()
      if (target > 0) {
        setupCard.minimumHeight = target
      }
    }

    pageMcpx = findViewById(R.id.page_mcpx)
    pageFlash = findViewById(R.id.page_flash)
    pageHdd = findViewById(R.id.page_hdd)
    mcpxPathText = findViewById(R.id.mcpx_path_text)
    flashPathText = findViewById(R.id.flash_path_text)
    hddPathText = findViewById(R.id.hdd_path_text)
    btnBack = findViewById(R.id.btn_wizard_back)
    btnNext = findViewById(R.id.btn_wizard_next)
    indicatorMcpx = findViewById(R.id.step_indicator_mcpx)
    indicatorFlash = findViewById(R.id.step_indicator_flash)
    indicatorHdd = findViewById(R.id.step_indicator_hdd)

    val btnPickMcpx: MaterialButton = findViewById(R.id.btn_pick_mcpx)
    val btnPickFlash: MaterialButton = findViewById(R.id.btn_pick_flash)
    val btnPickHdd: MaterialButton = findViewById(R.id.btn_pick_hdd)

    btnPickMcpx.setOnClickListener { pickMcpx.launch(arrayOf("application/octet-stream")) }
    btnPickFlash.setOnClickListener { pickFlash.launch(arrayOf("application/octet-stream")) }
    btnPickHdd.setOnClickListener { pickHdd.launch(arrayOf("application/x-qcow2", "application/octet-stream")) }

    btnBack.setOnClickListener { showStep(currentStep - 1) }
    btnNext.setOnClickListener {
      if (currentStep < 2) {
        showStep(currentStep + 1)
      } else {
        finishSetup()
      }
    }

    onBackPressedDispatcher.addCallback(
      this,
      object : OnBackPressedCallback(true) {
        override fun handleOnBackPressed() {
          if (currentStep > 0) {
            showStep(currentStep - 1)
          } else {
            finish()
          }
        }
      }
    )

    updateMcpxSelection()
    updateFlashSelection()
    updateHddSelection()

    showStep(0)
  }

  private fun showStep(step: Int) {
    currentStep = step.coerceIn(0, 2)
    pageMcpx.visibility = if (currentStep == 0) View.VISIBLE else View.GONE
    pageFlash.visibility = if (currentStep == 1) View.VISIBLE else View.GONE
    pageHdd.visibility = if (currentStep == 2) View.VISIBLE else View.GONE

    indicatorMcpx.setBackgroundResource(
      if (currentStep == 0) R.drawable.setup_wizard_indicator_active else R.drawable.setup_wizard_indicator_inactive
    )
    indicatorFlash.setBackgroundResource(
      if (currentStep == 1) R.drawable.setup_wizard_indicator_active else R.drawable.setup_wizard_indicator_inactive
    )
    indicatorHdd.setBackgroundResource(
      if (currentStep == 2) R.drawable.setup_wizard_indicator_active else R.drawable.setup_wizard_indicator_inactive
    )

    updateButtons()
  }

  private fun updateButtons() {
    btnBack.visibility = if (currentStep == 0) View.INVISIBLE else View.VISIBLE
    btnNext.text = getString(if (currentStep == 2) R.string.setup_finish else R.string.setup_next)
    btnNext.isEnabled =
      when (currentStep) {
        0 -> mcpxUri != null
        1 -> flashUri != null
        else -> hddUri != null
      }
  }

  private fun updateMcpxSelection() {
    val value = mcpxUri?.toString() ?: getString(R.string.setup_not_set)
    mcpxPathText.text = getString(R.string.setup_mcpx_value, value)
  }

  private fun updateFlashSelection() {
    val value = flashUri?.toString() ?: getString(R.string.setup_not_set)
    flashPathText.text = getString(R.string.setup_flash_value, value)
  }

  private fun updateHddSelection() {
    val value = hddUri?.toString() ?: getString(R.string.setup_not_set)
    hddPathText.text = getString(R.string.setup_hdd_value, value)
  }


  private fun finishSetup() {
    prefs.edit().putBoolean("setup_complete", true).apply()
    goToMain()
  }

  private fun goToMain() {
    startActivity(Intent(this, MainActivity::class.java))
    finish()
  }

  private fun persistUriPermission(uri: Uri) {
    val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION
    try {
      contentResolver.takePersistableUriPermission(uri, flags)
    } catch (_: SecurityException) {
    }
  }

  private fun isAllowedExtension(uri: Uri, allowed: Set<String>): Boolean {
    val name = contentResolver.query(uri, null, null, null, null)?.use { cursor ->
      val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
      if (nameIndex >= 0 && cursor.moveToFirst()) cursor.getString(nameIndex) else null
    } ?: uri.lastPathSegment
    val ext = name?.substringAfterLast('.', "")?.lowercase().orEmpty()
    if (ext.isEmpty()) return false
    return allowed.contains(ext)
  }

  private fun showExtensionError(allowed: Set<String>) {
    val pretty = allowed.sorted().joinToString(separator = ", ") { ".$it" }
    Toast.makeText(this, "Please pick a file with one of: $pretty", Toast.LENGTH_LONG).show()
  }
}
