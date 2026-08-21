package com.odpar.territorial_domain

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.system.Os
import android.system.OsConstants
import androidx.activity.result.contract.ActivityResultContracts
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import kotlin.math.max
import org.json.JSONArray
import org.json.JSONObject

class MainActivity : FlutterActivity() {
    private val channelName = "odpar/territorial_domain/android"
    private var pendingImageResult: MethodChannel.Result? = null
    private var pendingMusicResult: MethodChannel.Result? = null
    private lateinit var audioPlayer: NativeAudioPlayer
    private val worldIoExecutor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "odpar-world-io").apply { priority = Thread.NORM_PRIORITY - 1 }
    }
    private val noisyReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY && ::audioPlayer.isInitialized) {
                audioPlayer.pauseForBackground()
            }
        }
    }
    private var noisyReceiverRegistered = false

    external fun nativeSubmitPcm(
        payload: ByteArray,
        frameCount: Int,
        channels: Int,
        sampleRate: Int,
        encoding: Int,
        playbackTimeUs: Long,
    ): Int
    external fun nativeResetMusic()

    private val imagePicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        val result = pendingImageResult
        pendingImageResult = null
        if (uri == null) {
            result?.success(null)
            return@registerForActivityResult
        }
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: SecurityException) {
            // Some gallery providers grant a one-shot URI only. We copy the bytes immediately.
        }
        try {
            val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() }
            result?.success(bytes)
        } catch (error: Exception) {
            result?.error("IMAGE_READ", error.message, null)
        }
    }

    private val musicPicker = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
        val result = pendingMusicResult
        pendingMusicResult = null
        try {
            uris.forEach { uri ->
                try {
                    contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                } catch (_: SecurityException) {
                    // Provider may not support persistable grants; the queue will report unavailable later.
                }
            }
            audioPlayer.addUris(uris)
            result?.success(audioPlayer.state())
        } catch (error: Exception) {
            result?.error("MUSIC_PICK", error.message, null)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        audioPlayer = NativeAudioPlayer(
            context = this,
            submitPcm = { bytes, frames, channels, rate, encoding, timeUs ->
                nativeSubmitPcm(bytes, frames, channels, rate, encoding, timeUs)
            },
            resetAnalyzer = { nativeResetMusic() },
        )
        val filter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(noisyReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else {
            @Suppress("DEPRECATION")
            registerReceiver(noisyReceiver, filter)
        }
        noisyReceiverRegistered = true
    }

    override fun onStop() {
        /* No foreground playback service is shipped in v15. The safe Android contract is
         * therefore to stop local playback whenever the Activity leaves the foreground. */
        if (::audioPlayer.isInitialized && !isChangingConfigurations) audioPlayer.pauseForBackground()
        super.onStop()
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, channelName)
            .setMethodCallHandler(::handlePlatformCall)
    }

    override fun onDestroy() {
        if (noisyReceiverRegistered) {
            try { unregisterReceiver(noisyReceiver) } catch (_: IllegalArgumentException) { }
            noisyReceiverRegistered = false
        }
        if (::audioPlayer.isInitialized) audioPlayer.release()
        worldIoExecutor.shutdown()
        super.onDestroy()
    }

    private class HostCallException(val code: String, message: String) : RuntimeException(message)

    private fun runWorldIo(result: MethodChannel.Result, operation: () -> Any?) {
        try {
            worldIoExecutor.execute {
                try {
                    val value = operation()
                    runOnUiThread { result.success(value) }
                } catch (error: HostCallException) {
                    runOnUiThread { result.error(error.code, error.message, null) }
                } catch (error: Exception) {
                    runOnUiThread { result.error("ANDROID_HOST", error.message ?: error.javaClass.simpleName, null) }
                }
            }
        } catch (_: RejectedExecutionException) {
            result.error("ANDROID_HOST", "world I/O executor is shutting down", null)
        }
    }

    private fun handlePlatformCall(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "listWorlds" -> runWorldIo(result) { listWorldSlots() }
                "saveWorldSlot" -> {
                    val id = requireWorldId(call.argument<String>("id"))
                    val name = normalizeWorldName(call.argument<String>("name"))
                    val seed = call.argument<String>("seed") ?: return result.error("ARG", "seed missing", null)
                    val bytes = call.argument<ByteArray>("bytes") ?: return result.error("ARG", "bytes missing", null)
                    if (bytes.isEmpty()) return result.error("ARG", "save blob empty", null)
                    val createdAtArg = call.argument<Number>("createdAtMs")?.toLong()
                    val apiVersion = call.argument<Number>("apiVersion")?.toInt() ?: 0
                    val ffiAbiVersion = call.argument<Number>("ffiAbiVersion")?.toInt() ?: 0
                    val saveSchemaVersion = call.argument<Number>("saveSchemaVersion")?.toInt() ?: 0
                    runWorldIo(result) {
                        val dir = worldSlotDir(id)
                        val existing = readCommittedWorld(dir)
                        val now = System.currentTimeMillis()
                        val createdAt = existing?.meta?.optLong("createdAtMs", 0L)?.takeIf { it > 0L }
                            ?: createdAtArg?.takeIf { it > 0L }
                            ?: now
                        val meta = JSONObject()
                            .put("formatVersion", worldStorageFormatVersion)
                            .put("id", id)
                            .put("name", name)
                            .put("seed", seed)
                            .put("createdAtMs", createdAt)
                            .put("updatedAtMs", now)
                            .put("apiVersion", apiVersion)
                            .put("ffiAbiVersion", ffiAbiVersion)
                            .put("saveSchemaVersion", saveSchemaVersion)
                            .put("blobBytes", bytes.size)
                            .put("blobSha256", sha256Hex(bytes))
                        commitWorld(dir, meta, bytes)
                        true
                    }
                }
                "loadWorldSlot" -> {
                    val id = requireWorldId(call.argument<String>("id"), allowLegacy = true)
                    runWorldIo(result) {
                        if (id == legacyWorldId) {
                            val file = legacyWorldFile()
                            if (file.isFile) file.readBytes() else null
                        } else {
                            readCommittedWorld(worldSlotDir(id))?.bytes
                        }
                    }
                }
                "deleteWorldSlot" -> {
                    val id = requireWorldId(call.argument<String>("id"), allowLegacy = true)
                    runWorldIo(result) {
                        if (id == legacyWorldId) legacyWorldFile().delete() else worldSlotDir(id).deleteRecursively()
                    }
                }
                "renameWorldSlot" -> {
                    val id = requireWorldId(call.argument<String>("id"))
                    val name = normalizeWorldName(call.argument<String>("name"))
                    runWorldIo(result) {
                        val dir = worldSlotDir(id)
                        val current = readCommittedWorld(dir) ?: throw HostCallException("NOT_FOUND", "world metadata missing")
                        val meta = JSONObject(current.meta.toString())
                        meta.put("name", name)
                        meta.put("updatedAtMs", System.currentTimeMillis())
                        commitWorld(dir, meta, current.bytes)
                        worldMetadataMap(meta, legacy = false)
                    }
                }
                "saveSettings" -> {
                    val json = call.argument<String>("json") ?: "{}"
                    getSharedPreferences("odpar_v15", Context.MODE_PRIVATE).edit().putString("settings", json).apply()
                    result.success(true)
                }
                "loadSettings" -> result.success(
                    getSharedPreferences("odpar_v15", Context.MODE_PRIVATE).getString("settings", null),
                )
                "saveSkin" -> {
                    val face = call.argument<Int>("face") ?: return result.error("ARG", "face missing", null)
                    val bytes = call.argument<ByteArray>("rgba") ?: return result.error("ARG", "rgba missing", null)
                    if (face !in 0..5) return result.error("ARG", "invalid face", null)
                    runWorldIo(result) { atomicWrite(File(filesDir, "skins/face_$face.rgba"), bytes); true }
                }
                "loadSkin" -> {
                    val face = call.argument<Int>("face") ?: return result.error("ARG", "face missing", null)
                    if (face !in 0..5) return result.error("ARG", "invalid face", null)
                    runWorldIo(result) {
                        val file = File(filesDir, "skins/face_$face.rgba")
                        if (file.isFile) file.readBytes() else null
                    }
                }
                "pickImage" -> {
                    if (pendingImageResult != null) return result.error("BUSY", "image picker already open", null)
                    pendingImageResult = result
                    imagePicker.launch(arrayOf("image/*"))
                }
                "pickMusic" -> {
                    if (pendingMusicResult != null) return result.error("BUSY", "music picker already open", null)
                    pendingMusicResult = result
                    musicPicker.launch(arrayOf("audio/*"))
                }
                "musicCommand" -> {
                    val command = call.argument<String>("command") ?: return result.error("ARG", "command missing", null)
                    when (command) {
                        "play" -> audioPlayer.play()
                        "pause" -> audioPlayer.pause()
                        "next" -> audioPlayer.next()
                        "previous" -> audioPlayer.previous()
                        "seek" -> audioPlayer.seekTo(call.argument<Number>("positionMs")?.toLong() ?: 0L)
                        "volume" -> audioPlayer.setVolume(call.argument<Number>("value")?.toFloat() ?: 1f)
                        "shuffle" -> audioPlayer.setShuffle(call.argument<Boolean>("value") ?: false)
                        "repeat" -> audioPlayer.setRepeat(call.argument<Boolean>("value") ?: false)
                        "clear" -> audioPlayer.clear()
                        "gamePause" -> audioPlayer.pauseForGame()
                        "gameResume" -> audioPlayer.resumeFromGame()
                        "featuredCatalog" -> audioPlayer.addFeaturedCatalog()
                        else -> return result.error("ARG", "unknown music command", null)
                    }
                    result.success(audioPlayer.state())
                }
                "musicState" -> result.success(audioPlayer.state())
                else -> result.notImplemented()
            }
        } catch (error: Exception) {
            result.error("ANDROID_HOST", error.message ?: error.javaClass.simpleName, null)
        }
    }

    private val legacyWorldId = "legacy-v15"
    private val worldStorageFormatVersion = 2
    private val worldIdPattern = Regex("^[A-Za-z0-9_-]{1,64}$")

    private data class WorldCommit(val slot: Int?, val generation: Long, val meta: JSONObject, val bytes: ByteArray)
    private data class WorldPointer(val slot: Int, val generation: Long)

    private fun legacyWorldFile(): File = File(filesDir, "world/territorial_v15.odg")
    private fun worldSlotDir(id: String): File = File(File(filesDir, "worlds"), id)
    private fun commitDir(dir: File, slot: Int): File = File(dir, "commit_$slot")
    private fun pointerFile(dir: File): File = File(dir, "active.slot")

    private fun requireWorldId(value: String?, allowLegacy: Boolean = false): String {
        val id = value ?: throw IllegalArgumentException("world id missing")
        if (!worldIdPattern.matches(id)) throw IllegalArgumentException("invalid world id")
        if (!allowLegacy && id == legacyWorldId) throw IllegalArgumentException("reserved world id")
        return id
    }

    private fun normalizeWorldName(value: String?): String {
        val compact = (value ?: "Mundo").trim().replace(Regex("\s+"), " ")
        if (compact.isEmpty()) return "Mundo"
        return compact.take(48)
    }

    private fun parseWorldPointer(file: File): WorldPointer? {
        if (!file.isFile) return null
        val raw = try { file.readText(Charsets.US_ASCII).trim() } catch (_: Exception) { return null }
        raw.toIntOrNull()?.let { slot -> if (slot in 0..1) return WorldPointer(slot, 0L) }
        val fields = raw.split(':')
        if (fields.size != 3 || fields[0] != "2") return null
        val slot = fields[1].toIntOrNull() ?: return null
        val generation = fields[2].toLongOrNull() ?: return null
        if (slot !in 0..1 || generation <= 0L) return null
        return WorldPointer(slot, generation)
    }

    private fun readWorldCommitSlot(dir: File, slot: Int): WorldCommit? {
        if (slot !in 0..1) return null
        val commit = commitDir(dir, slot)
        val metaFile = File(commit, "meta.json")
        val blobFile = File(commit, "world.odg")
        if (!metaFile.isFile || !blobFile.isFile) return null
        return try {
            val meta = JSONObject(metaFile.readText(Charsets.UTF_8))
            val format = meta.optInt("formatVersion", 1)
            if (format !in 1..worldStorageFormatVersion) return null
            val generation = if (format >= 2) meta.optLong("storageGeneration", 0L) else 0L
            val bytes = blobFile.readBytes()
            if (bytes.isEmpty() || !worldBlobMatches(meta, bytes)) return null
            WorldCommit(slot, generation, meta, bytes)
        } catch (_: Exception) { null }
    }

    private fun readLegacyWorldCommit(dir: File): WorldCommit? {
        val metaFile = File(dir, "meta.json")
        val blobFile = File(dir, "world.odg")
        if (!metaFile.isFile || !blobFile.isFile) return null
        return try {
            val meta = JSONObject(metaFile.readText(Charsets.UTF_8))
            val bytes = blobFile.readBytes()
            if (bytes.isEmpty() || !worldBlobMatches(meta, bytes)) return null
            WorldCommit(null, 0L, meta, bytes)
        } catch (_: Exception) { null }
    }

    private fun readCommittedWorld(dir: File): WorldCommit? {
        val pointerFile = pointerFile(dir)
        val legacy = readLegacyWorldCommit(dir)
        /* The pointer rename is the commit point. Staged A/B slots are never authoritative
         * without it, including the first-ever save interrupted before pointer creation. */
        if (!pointerFile.isFile) return legacy
        val pointer = parseWorldPointer(pointerFile) ?: return legacy
        val active = readWorldCommitSlot(dir, pointer.slot)
        if (pointer.generation == 0L) {
            if (active != null) return active
            return readWorldCommitSlot(dir, 1 - pointer.slot) ?: legacy
        }
        if (active != null && active.generation == pointer.generation) return active
        val fallback = (0..1)
            .mapNotNull { readWorldCommitSlot(dir, it) }
            .filter { it.generation > 0L && it.generation < pointer.generation }
            .maxByOrNull { it.generation }
        return fallback ?: legacy
    }

    private fun worldMetadataMap(meta: JSONObject, legacy: Boolean, corrupt: Boolean = false): Map<String, Any?> = mapOf(
        "id" to meta.optString("id", ""),
        "name" to meta.optString("name", "Mundo"),
        "seed" to meta.optString("seed", "1"),
        "createdAtMs" to meta.optLong("createdAtMs", 0L),
        "updatedAtMs" to meta.optLong("updatedAtMs", 0L),
        "apiVersion" to meta.optInt("apiVersion", 0),
        "ffiAbiVersion" to meta.optInt("ffiAbiVersion", 0),
        "saveSchemaVersion" to meta.optInt("saveSchemaVersion", 0),
        "legacy" to legacy,
        "corrupt" to corrupt,
    )

    private fun listWorldSlots(): List<Map<String, Any?>> {
        val rows = mutableListOf<Map<String, Any?>>()
        val root = File(filesDir, "worlds")
        root.listFiles()?.filter { it.isDirectory }?.forEach { dir ->
            val committed = readCommittedWorld(dir) ?: return@forEach
            val meta = committed.meta
            val id = meta.optString("id", "")
            if (!worldIdPattern.matches(id) || id != dir.name || id == legacyWorldId) return@forEach
            rows.add(worldMetadataMap(meta, legacy = false, corrupt = false))
        }
        val legacy = legacyWorldFile()
        if (legacy.isFile) {
            val modified = legacy.lastModified().takeIf { it > 0L } ?: 0L
            rows.add(mapOf(
                "id" to legacyWorldId, "name" to "Dominio legado v15", "seed" to "1",
                "createdAtMs" to modified, "updatedAtMs" to modified,
                "apiVersion" to 15, "ffiAbiVersion" to 2, "saveSchemaVersion" to 0,
                "legacy" to true, "corrupt" to false,
            ))
        }
        return rows.sortedByDescending { (it["updatedAtMs"] as? Long) ?: 0L }
    }

    private fun sha256Hex(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun worldBlobMatches(meta: JSONObject, bytes: ByteArray): Boolean {
        val expectedBytes = meta.optLong("blobBytes", -1L)
        val expectedHash = meta.optString("blobSha256", "")
        if (expectedBytes < 0L || expectedHash.length != 64) return false
        return expectedBytes == bytes.size.toLong() && expectedHash.equals(sha256Hex(bytes), ignoreCase = true)
    }

    private fun commitWorld(dir: File, sourceMeta: JSONObject, bytes: ByteArray) {
        if (bytes.isEmpty()) throw IllegalArgumentException("save blob empty")
        dir.mkdirs()
        val current = readCommittedWorld(dir)
        val target = when (current?.slot) { 0 -> 1; 1 -> 0; else -> 0 }
        val pointerGeneration = parseWorldPointer(pointerFile(dir))?.generation ?: 0L
        val slotGeneration = (0..1).mapNotNull { readWorldCommitSlot(dir, it)?.generation }.maxOrNull() ?: 0L
        val maxGeneration = maxOf(pointerGeneration, slotGeneration, current?.generation ?: 0L)
        if (maxGeneration == Long.MAX_VALUE) throw IllegalStateException("world save generation exhausted")
        val nextGeneration = maxGeneration + 1L
        val meta = JSONObject(sourceMeta.toString())
            .put("formatVersion", worldStorageFormatVersion)
            .put("storageGeneration", nextGeneration)
            .put("blobBytes", bytes.size)
            .put("blobSha256", sha256Hex(bytes))
        val targetDir = commitDir(dir, target)
        targetDir.mkdirs()
        atomicWrite(File(targetDir, "world.odg"), bytes)
        atomicWrite(File(targetDir, "meta.json"), meta.toString().toByteArray(Charsets.UTF_8))
        val staged = readWorldCommitSlot(dir, target)
            ?: throw IllegalStateException("staged world commit failed validation")
        if (staged.generation != nextGeneration || staged.meta.optString("id") != meta.optString("id") || !staged.bytes.contentEquals(bytes)) {
            throw IllegalStateException("staged world commit does not match requested save")
        }
        atomicWrite(pointerFile(dir), "2:$target:$nextGeneration\n".toByteArray(Charsets.US_ASCII))
    }

    private fun fsyncDirectory(dir: File) {
        if (!dir.isDirectory) return
        val fd = Os.open(dir.absolutePath, OsConstants.O_RDONLY or OsConstants.O_DIRECTORY, 0)
        try { Os.fsync(fd) } finally { Os.close(fd) }
    }

    private fun atomicWrite(file: File, bytes: ByteArray) {
        val parent = file.parentFile ?: throw IllegalArgumentException("file has no parent")
        parent.mkdirs()
        val temp = File(parent, ".${file.name}.tmp")
        if (temp.exists() && !temp.delete()) throw IllegalStateException("cannot clear stale temp for ${file.name}")
        FileOutputStream(temp).use { output ->
            output.write(bytes)
            output.flush()
            output.fd.sync()
        }
        Os.rename(temp.absolutePath, file.absolutePath)
        fsyncDirectory(parent)
    }

    companion object {
        init { System.loadLibrary("odpar_territorial_domain") }
    }
}

private data class QueueTrack(
    val uri: Uri,
    val title: String,
    val artist: String,
    val durationMs: Long,
    var available: Boolean = true,
    val featured: Boolean = false,
)

private data class FeaturedTrack(
    val fileName: String,
    val title: String,
    val kind: String,
    val durationMs: Long,
)

private class NativeAudioPlayer(
    private val context: Context,
    private val submitPcm: (ByteArray, Int, Int, Int, Int, Long) -> Int,
    private val resetAnalyzer: () -> Unit,
) {
    private val lock = Object()
    private val queue = mutableListOf<QueueTrack>()
    private val released = AtomicBoolean(false)
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val prefs = context.getSharedPreferences("odpar_music_queue_v1", Context.MODE_PRIVATE)
    @Volatile private var playing = false
    @Volatile private var index = 0
    @Volatile private var positionMs = 0L
    @Volatile private var generation = 1L
    @Volatile private var volume = 1f
    @Volatile private var shuffle = false
    @Volatile private var repeat = false
    @Volatile private var activeTrack: AudioTrack? = null
    private var resumeAfterFocusGain = false
    private var resumeAfterGamePause = false
    private var duckedForFocus = false
    private var focusRequest: AudioFocusRequest? = null

    private val featuredCatalog = listOf(
        FeaturedTrack("01_half_second_lag.mp3", "Half-Second Lag", "Original Track", 252504L),
        FeaturedTrack("02_not_quiet.mp3", "Not Quiet", "Original Track", 246984L),
        FeaturedTrack("03_hear_myself_breathe.mp3", "Hear Myself Breathe", "Original Track", 254232L),
        FeaturedTrack("04_quiet_not_empty.mp3", "Quiet, Not Empty", "Original Track", 301200L),
        FeaturedTrack("05_i_dont_go_with_it.mp3", "I Don’t Go With It", "Original Track", 307392L),
        FeaturedTrack("06_not_you_just_light.mp3", "Not You, Just Light", "Original Track", 324960L),
        FeaturedTrack("07_after_the_last_color.mp3", "After the Last Color", "Original Track", 412032L),
        FeaturedTrack("08_half_second_lag_instrumental_rework.mp3", "Half-Second Lag — Instrumental Rework", "Instrumental Rework", 229752L),
        FeaturedTrack("09_not_quiet_instrumental_rework.mp3", "Not Quiet — Instrumental Rework", "Instrumental Rework", 214944L),
        FeaturedTrack("10_hear_myself_breathe_instrumental_rework.mp3", "Hear Myself Breathe — Instrumental Rework", "Instrumental Rework", 202824L),
        FeaturedTrack("11_quiet_not_empty_instrumental_rework.mp3", "Quiet, Not Empty — Instrumental Rework", "Instrumental Rework", 189024L),
        FeaturedTrack("12_i_dont_go_with_it_instrumental_rework.mp3", "I Don’t Go With It — Instrumental Rework", "Instrumental Rework", 195024L),
    )

    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                duckedForFocus = false
                activeTrack?.setVolume(volume)
                if (resumeAfterFocusGain) { resumeAfterFocusGain = false; play() }
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                duckedForFocus = true
                activeTrack?.setVolume(volume * 0.20f)
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                resumeAfterFocusGain = playing
                pauseInternal(reset = true, abandonFocus = false)
            }
            AudioManager.AUDIOFOCUS_LOSS -> {
                resumeAfterFocusGain = false
                pauseInternal(reset = true, abandonFocus = true)
            }
        }
    }

    init {
        restoreQueue()
        synchronized(lock) {
            if (queue.isEmpty()) addFeaturedCatalogLocked()
        }
        thread(name = "ODPAR-Audio", isDaemon = true, priority = Thread.NORM_PRIORITY) { decodeLoop() }
    }

    private fun featuredFile(meta: FeaturedTrack): File {
        val dir = File(context.filesDir, "featured_music/afterimage_0_2")
        dir.mkdirs()
        val out = File(dir, meta.fileName)
        if (!out.isFile || out.length() == 0L) {
            val assetPath = "flutter_assets/assets/music/afterimage_0_2/${meta.fileName}"
            context.assets.open(assetPath).use { input ->
                val temp = File(dir, ".${meta.fileName}.tmp")
                FileOutputStream(temp).use { output -> input.copyTo(output); output.flush(); output.fd.sync() }
                if (out.exists() && !out.delete()) throw IllegalStateException("cannot replace featured track ${meta.fileName}")
                if (!temp.renameTo(out)) throw IllegalStateException("cannot install featured track ${meta.fileName}")
            }
        }
        return out
    }

    private fun featuredTrackForUri(uri: Uri): QueueTrack? {
        val path = uri.path ?: return null
        val meta = featuredCatalog.firstOrNull { path.endsWith("/${it.fileName}") } ?: return null
        val file = File(path)
        return QueueTrack(uri, meta.title, "kaost032 · AFTERIMAGE 0.2", meta.durationMs, file.isFile, true)
    }

    private fun addFeaturedCatalogLocked() {
        featuredCatalog.forEach { meta ->
            val file = featuredFile(meta)
            val uri = Uri.fromFile(file)
            if (queue.none { it.uri == uri }) queue += QueueTrack(
                uri, meta.title, "kaost032 · AFTERIMAGE 0.2", meta.durationMs, true, true,
            )
        }
        if (index !in queue.indices) index = 0
        persistQueueLocked()
    }

    fun addFeaturedCatalog() {
        synchronized(lock) {
            addFeaturedCatalogLocked()
            generation += 1
            lock.notifyAll()
        }
    }

    private fun describeUri(uri: Uri): QueueTrack {
        featuredTrackForUri(uri)?.let { return it }
        val retriever = MediaMetadataRetriever()
        return try {
            retriever.setDataSource(context, uri)
            QueueTrack(
                uri,
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE) ?: uri.lastPathSegment ?: "Audio",
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST) ?: "Local",
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L,
                true,
            )
        } catch (_: Exception) {
            QueueTrack(uri, uri.lastPathSegment ?: "Audio no disponible", "Local", 0L, false)
        } finally { retriever.release() }
    }

    private fun persistQueueLocked() {
        val uris = JSONArray()
        queue.forEach { uris.put(it.uri.toString()) }
        prefs.edit()
            .putString("uris", uris.toString())
            .putInt("index", index)
            .putLong("position_ms", positionMs)
            .putFloat("volume", volume)
            .putBoolean("shuffle", shuffle)
            .putBoolean("repeat", repeat)
            .apply()
    }

    private fun restoreQueue() {
        synchronized(lock) {
            volume = prefs.getFloat("volume", 1f).coerceIn(0f, 1f)
            shuffle = prefs.getBoolean("shuffle", false)
            repeat = prefs.getBoolean("repeat", false)
            val raw = prefs.getString("uris", null)
            if (!raw.isNullOrBlank()) {
                try {
                    val list = JSONArray(raw)
                    for (i in 0 until list.length()) {
                        val uri = Uri.parse(list.optString(i, ""))
                        if (uri.toString().isNotEmpty() && queue.none { it.uri == uri }) queue += describeUri(uri)
                    }
                } catch (_: Exception) { queue.clear() }
            }
            index = prefs.getInt("index", 0).coerceIn(0, max(0, queue.lastIndex))
            val duration = queue.getOrNull(index)?.durationMs ?: 0L
            positionMs = prefs.getLong("position_ms", 0L).coerceIn(0L, max(0L, duration))
        }
    }

    fun addUris(uris: List<Uri>) {
        synchronized(lock) {
            uris.forEach { uri -> if (queue.none { it.uri == uri }) queue += describeUri(uri) }
            if (index !in queue.indices) index = 0
            persistQueueLocked()
            generation += 1
            lock.notifyAll()
        }
    }

    fun play() {
        synchronized(lock) {
            if (queue.isEmpty()) return
            requestAudioFocus()
            playing = true
            activeTrack?.setVolume(if (duckedForFocus) volume * 0.20f else volume)
            activeTrack?.play()
            lock.notifyAll()
        }
    }

    private fun pauseInternal(reset: Boolean, abandonFocus: Boolean) {
        playing = false
        try { activeTrack?.pause() } catch (_: IllegalStateException) { }
        if (reset) resetAnalyzer()
        synchronized(lock) { persistQueueLocked() }
        if (abandonFocus) abandonAudioFocus()
    }

    fun pause() { resumeAfterFocusGain = false; resumeAfterGamePause = false; pauseInternal(reset = true, abandonFocus = false) }
    fun pauseForGame() {
        synchronized(lock) { resumeAfterGamePause = playing }
        if (resumeAfterGamePause) pauseInternal(reset = true, abandonFocus = false)
    }
    fun resumeFromGame() {
        val shouldResume = synchronized(lock) { val v = resumeAfterGamePause; resumeAfterGamePause = false; v }
        if (shouldResume) play()
    }
    fun pauseForBackground() { resumeAfterFocusGain = false; resumeAfterGamePause = false; pauseInternal(reset = true, abandonFocus = true) }

    fun next() = changeTrack(1)
    fun previous() = changeTrack(-1)

    private fun changeTrack(delta: Int) {
        synchronized(lock) {
            if (queue.isEmpty()) return
            index = if (shuffle && queue.size > 1) {
                var candidate = ((System.nanoTime() ushr 8) % queue.size).toInt()
                if (candidate == index) candidate = (candidate + 1) % queue.size
                candidate
            } else (index + delta + queue.size) % queue.size
            positionMs = 0L
            generation += 1
            resetAnalyzer()
            persistQueueLocked()
            lock.notifyAll()
        }
    }

    fun seekTo(value: Long) {
        synchronized(lock) {
            val duration = queue.getOrNull(index)?.durationMs ?: 0L
            positionMs = value.coerceIn(0L, max(0L, duration))
            generation += 1
            resetAnalyzer()
            persistQueueLocked()
            lock.notifyAll()
        }
    }

    fun setVolume(value: Float) {
        synchronized(lock) {
            volume = value.coerceIn(0f, 1f)
            activeTrack?.setVolume(if (duckedForFocus) volume * 0.20f else volume)
            persistQueueLocked()
        }
    }
    fun setShuffle(value: Boolean) { synchronized(lock) { shuffle = value; persistQueueLocked() } }
    fun setRepeat(value: Boolean) { synchronized(lock) { repeat = value; persistQueueLocked() } }

    fun clear() {
        synchronized(lock) {
            playing = false
            queue.clear(); index = 0; positionMs = 0L; generation += 1
            resetAnalyzer(); persistQueueLocked(); lock.notifyAll()
        }
    }

    fun state(): Map<String, Any?> = synchronized(lock) {
        val current = queue.getOrNull(index)
        mapOf(
            "playing" to playing,
            "index" to index,
            "positionMs" to positionMs,
            "durationMs" to (current?.durationMs ?: 0L),
            "title" to (current?.title ?: ""),
            "artist" to (current?.artist ?: ""),
            "volume" to volume.toDouble(),
            "shuffle" to shuffle,
            "repeat" to repeat,
            "queue" to queue.map { mapOf("uri" to it.uri.toString(), "title" to it.title, "artist" to it.artist, "durationMs" to it.durationMs, "available" to it.available, "featured" to it.featured) },
            "featuredCatalog" to featuredCatalog.map { mapOf("title" to it.title, "artist" to "kaost032", "kind" to it.kind, "durationMs" to it.durationMs) },
            "featuredCatalogName" to "AFTERIMAGE 0.2",
            "featuredOriginalCount" to 7,
            "featuredReworkCount" to 5,
        )
    }

    fun release() {
        released.set(true); playing = false; generation += 1
        synchronized(lock) { lock.notifyAll() }
        activeTrack?.release(); activeTrack = null
        abandonAudioFocus()
    }

    private fun decodeLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        while (!released.get()) {
            val track: QueueTrack
            val localGeneration: Long
            val startMs: Long
            synchronized(lock) {
                while (!released.get() && (!playing || queue.isEmpty())) lock.wait(250L)
                if (released.get()) return
                track = queue[index.coerceIn(0, queue.lastIndex)]
                localGeneration = generation
                startMs = positionMs
            }
            var decodeFailed = false
            val reachedEnd = try { decodeTrack(track, localGeneration, startMs) } catch (_: Exception) { decodeFailed = true; false }
            synchronized(lock) {
                if (released.get()) return
                if (generation != localGeneration) continue
                if (decodeFailed) {
                    queue.getOrNull(index)?.available = false
                    val next = queue.indices.firstOrNull { it != index && queue[it].available }
                    positionMs = 0L
                    if (next != null) { index = next; generation += 1 } else playing = false
                    persistQueueLocked(); resetAnalyzer()
                } else if (reachedEnd) {
                    positionMs = 0L
                    if (repeat) generation += 1
                    else if (queue.isNotEmpty()) { index = (index + 1) % queue.size; generation += 1 }
                    persistQueueLocked(); resetAnalyzer()
                }
            }
        }
    }

    private fun decodeTrack(track: QueueTrack, localGeneration: Long, startMs: Long): Boolean {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        var audioTrack: AudioTrack? = null
        try {
            extractor.setDataSource(context, track.uri, null)
            var trackIndex = -1
            var format: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val candidate = extractor.getTrackFormat(i)
                val mime = candidate.getString(MediaFormat.KEY_MIME) ?: continue
                if (mime.startsWith("audio/")) { trackIndex = i; format = candidate; break }
            }
            if (trackIndex < 0 || format == null) return false
            extractor.selectTrack(trackIndex)
            if (startMs > 0L) extractor.seekTo(startMs * 1000L, MediaExtractor.SEEK_TO_CLOSEST_SYNC)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: return false
            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0); codec.start()
            val info = MediaCodec.BufferInfo()
            var inputDone = false
            var outputDone = false
            var sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            var channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT).coerceIn(1, 2)
            var pcmEncoding = AudioFormat.ENCODING_PCM_16BIT
            while (!outputDone && !released.get() && localGeneration == generation) {
                if (!playing) {
                    synchronized(lock) { if (!playing && localGeneration == generation) lock.wait(100L) }
                    continue
                }
                if (!inputDone) {
                    val inputIndex = codec.dequeueInputBuffer(10_000)
                    if (inputIndex >= 0) {
                        val input = codec.getInputBuffer(inputIndex) ?: continue
                        val size = extractor.readSampleData(input, 0)
                        if (size < 0) {
                            codec.queueInputBuffer(inputIndex, 0, 0, 0L, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputDone = true
                        } else {
                            codec.queueInputBuffer(inputIndex, 0, size, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }
                when (val outputIndex = codec.dequeueOutputBuffer(info, 10_000)) {
                    MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val out = codec.outputFormat
                        sampleRate = out.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        channels = out.getInteger(MediaFormat.KEY_CHANNEL_COUNT).coerceIn(1, 2)
                        pcmEncoding = if (Build.VERSION.SDK_INT >= 24 && out.containsKey(MediaFormat.KEY_PCM_ENCODING))
                            out.getInteger(MediaFormat.KEY_PCM_ENCODING) else AudioFormat.ENCODING_PCM_16BIT
                        if (pcmEncoding != AudioFormat.ENCODING_PCM_FLOAT) pcmEncoding = AudioFormat.ENCODING_PCM_16BIT
                        audioTrack?.release()
                        audioTrack = createAudioTrack(sampleRate, channels, pcmEncoding)
                        activeTrack = audioTrack
                        audioTrack.setVolume(volume)
                        if (playing) audioTrack.play()
                    }
                    MediaCodec.INFO_TRY_AGAIN_LATER -> Unit
                    else -> if (outputIndex >= 0) {
                        val buffer = codec.getOutputBuffer(outputIndex)
                        if (buffer != null && info.size > 0) {
                            if (audioTrack == null) {
                                audioTrack = createAudioTrack(sampleRate, channels, pcmEncoding)
                                activeTrack = audioTrack; audioTrack.setVolume(volume); audioTrack.play()
                            }
                            buffer.position(info.offset); buffer.limit(info.offset + info.size)
                            val bytes = ByteArray(info.size); buffer.get(bytes)
                            val bytesPerSample = if (pcmEncoding == AudioFormat.ENCODING_PCM_FLOAT) 4 else 2
                            val frames = info.size / max(1, channels * bytesPerSample)
                            submitPcm(bytes, frames, channels, sampleRate, pcmEncoding, info.presentationTimeUs)
                            audioTrack.write(bytes, 0, bytes.size, AudioTrack.WRITE_BLOCKING)
                            positionMs = info.presentationTimeUs / 1000L
                        }
                        outputDone = (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                        codec.releaseOutputBuffer(outputIndex, false)
                    }
                }
            }
            return outputDone && localGeneration == generation
        } finally {
            if (activeTrack === audioTrack) activeTrack = null
            try { audioTrack?.stop() } catch (_: IllegalStateException) { }
            audioTrack?.release()
            try { codec?.stop() } catch (_: IllegalStateException) { }
            codec?.release(); extractor.release()
        }
    }

    private fun createAudioTrack(sampleRate: Int, channels: Int, encoding: Int): AudioTrack {
        val mask = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val min = AudioTrack.getMinBufferSize(sampleRate, mask, encoding).coerceAtLeast(sampleRate / 10)
        return AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder().setSampleRate(sampleRate).setEncoding(encoding).setChannelMask(mask).build())
            .setBufferSizeInBytes(min * 2)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
    }

    private fun requestAudioFocus() {
        if (Build.VERSION.SDK_INT >= 26) {
            val request = focusRequest ?: AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(focusListener).build().also { focusRequest = it }
            audioManager.requestAudioFocus(request)
        } else {
            @Suppress("DEPRECATION")
            audioManager.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
        }
    }

    private fun abandonAudioFocus() {
        if (Build.VERSION.SDK_INT >= 26) focusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
        else @Suppress("DEPRECATION") audioManager.abandonAudioFocus(focusListener)
    }
}
