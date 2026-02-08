package com.izzy2lost.x1box

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import com.google.android.material.button.MaterialButton
import java.util.ArrayDeque
import java.util.Locale

class GameLibraryActivity : AppCompatActivity() {
  private data class GameEntry(
    val title: String,
    val uri: Uri,
    val relativePath: String,
    val sizeBytes: Long
  )

  private val prefs by lazy { getSharedPreferences("x1box_prefs", MODE_PRIVATE) }
  private val gameExts = setOf("iso", "xiso", "cso", "cci")

  private lateinit var folderText: TextView
  private lateinit var loadingSpinner: ProgressBar
  private lateinit var loadingText: TextView
  private lateinit var emptyText: TextView
  private lateinit var gamesContainer: LinearLayout
  private lateinit var btnChangeFolder: MaterialButton

  private var gamesFolderUri: Uri? = null
  private var scanGeneration = 0

  private val pickGamesFolder =
    registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
      if (uri != null) {
        persistUriPermission(uri)
        gamesFolderUri = uri
        prefs.edit().putString("gamesFolderUri", uri.toString()).apply()
        loadGames()
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_game_library)

    folderText = findViewById(R.id.library_folder_text)
    loadingSpinner = findViewById(R.id.library_loading)
    loadingText = findViewById(R.id.library_loading_text)
    emptyText = findViewById(R.id.library_empty_text)
    gamesContainer = findViewById(R.id.library_games_container)
    btnChangeFolder = findViewById(R.id.btn_change_games_folder)

    gamesFolderUri = prefs.getString("gamesFolderUri", null)?.let(Uri::parse)

    btnChangeFolder.setOnClickListener {
      pickGamesFolder.launch(gamesFolderUri)
    }

    if (!isFolderReady(gamesFolderUri)) {
      folderText.text = getString(R.string.library_no_folder)
      Toast.makeText(this, getString(R.string.setup_pick_disc), Toast.LENGTH_SHORT).show()
      pickGamesFolder.launch(gamesFolderUri)
      return
    }

    loadGames()
  }

  private fun loadGames() {
    val folderUri = gamesFolderUri
    if (!isFolderReady(folderUri)) {
      setLoading(false)
      setGames(emptyList())
      folderText.text = getString(R.string.library_no_folder)
      return
    }

    folderText.text = getString(R.string.library_folder_value, formatTreeLabel(folderUri!!))
    setLoading(true)

    val generation = ++scanGeneration
    Thread {
      val games = scanFolderForGames(folderUri)
      runOnUiThread {
        if (generation != scanGeneration) {
          return@runOnUiThread
        }
        setLoading(false)
        setGames(games)
      }
    }.start()
  }

  private fun setLoading(loading: Boolean) {
    loadingSpinner.visibility = if (loading) View.VISIBLE else View.GONE
    loadingText.visibility = if (loading) View.VISIBLE else View.GONE
  }

  private fun setGames(games: List<GameEntry>) {
    gamesContainer.removeAllViews()
    emptyText.visibility = if (games.isEmpty()) View.VISIBLE else View.GONE
    if (games.isEmpty()) {
      return
    }

    val inflater = LayoutInflater.from(this)
    for (game in games) {
      val item = inflater.inflate(R.layout.item_game_entry, gamesContainer, false)
      val nameText = item.findViewById<TextView>(R.id.game_name_text)
      val sizeText = item.findViewById<TextView>(R.id.game_size_text)
      val pathText = item.findViewById<TextView>(R.id.game_path_text)

      nameText.text = game.title
      sizeText.text = getString(R.string.library_game_size, formatSize(game.sizeBytes))
      pathText.text = getString(R.string.library_game_path, game.relativePath)

      item.setOnClickListener { launchGame(game) }
      gamesContainer.addView(item)
    }
  }

  private fun launchGame(game: GameEntry) {
    persistUriPermission(game.uri)
    prefs.edit()
      .putString("dvdUri", game.uri.toString())
      .remove("dvdPath")
      .putBoolean("skip_game_picker", false)
      .apply()

    startActivity(Intent(this, MainActivity::class.java))
    finish()
  }

  private fun scanFolderForGames(folderUri: Uri): List<GameEntry> {
    val root = DocumentFile.fromTreeUri(this, folderUri) ?: return emptyList()
    val stack = ArrayDeque<Pair<DocumentFile, String>>()
    stack.add(root to "")

    val games = ArrayList<GameEntry>()
    while (stack.isNotEmpty()) {
      val (node, prefix) = stack.removeLast()
      val files = try {
        node.listFiles()
      } catch (_: Exception) {
        emptyArray()
      }
      for (child in files) {
        val name = child.name ?: continue
        if (child.isDirectory) {
          stack.add(child to (prefix + name + "/"))
          continue
        }
        if (!child.isFile || !isSupportedGame(name)) {
          continue
        }
        games.add(
          GameEntry(
            title = toGameTitle(name),
            uri = child.uri,
            relativePath = prefix + name,
            sizeBytes = child.length()
          )
        )
      }
    }

    games.sortBy { it.title.lowercase(Locale.ROOT) }
    return games
  }

  private fun isSupportedGame(name: String): Boolean {
    val lower = name.lowercase(Locale.ROOT)
    if (lower.endsWith(".xiso.iso")) {
      return true
    }
    val ext = lower.substringAfterLast('.', "")
    return ext.isNotEmpty() && gameExts.contains(ext)
  }

  private fun toGameTitle(fileName: String): String {
    val lower = fileName.lowercase(Locale.ROOT)
    return when {
      lower.endsWith(".xiso.iso") -> fileName.dropLast(".xiso.iso".length)
      fileName.contains('.') -> fileName.substringBeforeLast('.')
      else -> fileName
    }
  }

  private fun formatSize(bytes: Long): String {
    if (bytes <= 0L) {
      return "Unknown"
    }
    val units = arrayOf("B", "KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var unitIndex = 0
    while (value >= 1024.0 && unitIndex < units.lastIndex) {
      value /= 1024.0
      unitIndex++
    }
    return String.format(Locale.US, "%.1f %s", value, units[unitIndex])
  }

  private fun isFolderReady(uri: Uri?): Boolean {
    if (uri == null || !hasPersistedReadPermission(uri)) {
      return false
    }
    val root = DocumentFile.fromTreeUri(this, uri) ?: return false
    return root.exists() && root.isDirectory
  }

  private fun persistUriPermission(uri: Uri) {
    val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION
    try {
      contentResolver.takePersistableUriPermission(uri, flags)
    } catch (_: SecurityException) {
    }
  }

  private fun hasPersistedReadPermission(uri: Uri): Boolean {
    return contentResolver.persistedUriPermissions.any { perm ->
      perm.uri == uri && perm.isReadPermission
    }
  }

  private fun formatTreeLabel(uri: Uri): String {
    val name = DocumentFile.fromTreeUri(this, uri)?.name
    if (!name.isNullOrBlank()) {
      return name
    }
    return uri.toString()
  }
}
