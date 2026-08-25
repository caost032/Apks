package com.odpar.territorial_domain.greenfield

import android.view.Surface
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry

class NativeRenderPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    private data class RenderTexture(
        val producer: TextureRegistry.SurfaceProducer,
        val serviceHandle: Long,
    )

    private var channel: MethodChannel? = null
    private var textureRegistry: TextureRegistry? = null
    private val textures = mutableMapOf<Long, RenderTexture>()

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        textureRegistry = binding.textureRegistry
        channel = MethodChannel(
            binding.binaryMessenger,
            "odpar.greenfield/native_render",
        ).also { it.setMethodCallHandler(this) }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel?.setMethodCallHandler(null)
        channel = null
        // Each RenderTexture owns a native service reference. Remove lifecycle
        // callbacks and detach the surface before releasing that reference, so
        // a late SurfaceProducer callback can never dereference freed service memory.
        textures.values.forEach { texture ->
            texture.producer.setCallback(null)
            nativeDetachSurface(texture.serviceHandle)
            texture.producer.release()
            nativeReleaseService(texture.serviceHandle)
        }
        textures.clear()
        textureRegistry = null
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "createTexture" -> createTexture(call, result)
            "resizeTexture" -> resizeTexture(call, result)
            "releaseTexture" -> releaseTexture(call, result)
            else -> result.notImplemented()
        }
    }

    private fun createTexture(call: MethodCall, result: MethodChannel.Result) {
        val registry = textureRegistry
        if (registry == null) {
            result.error("NO_TEXTURE_REGISTRY", "Flutter texture registry unavailable", null)
            return
        }
        val serviceHandle = call.argument<Number>("serviceHandle")?.toLong()
        val width = call.argument<Number>("width")?.toInt()
        val height = call.argument<Number>("height")?.toInt()
        if (serviceHandle == null || serviceHandle == 0L || width == null || height == null ||
            width <= 0 || height <= 0
        ) {
            result.error("BAD_ARGUMENT", "Invalid native render arguments", null)
            return
        }

        nativeRetainService(serviceHandle)
        var producer: TextureRegistry.SurfaceProducer? = null
        try {
            producer = registry.createSurfaceProducer()
            producer.setSize(width, height)
            val texture = RenderTexture(producer, serviceHandle)
            producer.setCallback(
                object : TextureRegistry.SurfaceProducer.Callback {
                    override fun onSurfaceAvailable() {
                        attachCurrentSurface(texture)
                    }

                    override fun onSurfaceCleanup() {
                        nativeDetachSurface(texture.serviceHandle)
                    }
                },
            )
            // setCallback covers replacement surfaces. The initial surface is
            // already available and is attached once here.
            attachCurrentSurface(texture)
            textures[producer.id()] = texture
            result.success(producer.id())
        } catch (error: Throwable) {
            producer?.setCallback(null)
            nativeDetachSurface(serviceHandle)
            producer?.release()
            nativeReleaseService(serviceHandle)
            result.error("TEXTURE_CREATE_FAILED", error.message ?: error.javaClass.simpleName, null)
        }
    }

    private fun attachCurrentSurface(texture: RenderTexture) {
        val surface: Surface = texture.producer.getSurface()
        nativeAttachSurface(texture.serviceHandle, surface)
        // SurfaceProducer owns the Surface object; do not release/cache it here.
    }

    private fun resizeTexture(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        val width = call.argument<Number>("width")?.toInt()
        val height = call.argument<Number>("height")?.toInt()
        val texture = if (textureId == null) null else textures[textureId]
        if (texture == null || width == null || height == null || width <= 0 || height <= 0) {
            result.error("BAD_ARGUMENT", "Unknown texture or invalid extent", null)
            return
        }
        texture.producer.setSize(width, height)
        result.success(null)
    }

    private fun releaseTexture(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        val texture = if (textureId == null) null else textures.remove(textureId)
        if (texture == null) {
            result.success(null)
            return
        }
        texture.producer.setCallback(null)
        nativeDetachSurface(texture.serviceHandle)
        texture.producer.release()
        nativeReleaseService(texture.serviceHandle)
        result.success(null)
    }

    private external fun nativeAttachSurface(serviceHandle: Long, surface: Surface)
    private external fun nativeDetachSurface(serviceHandle: Long)
    private external fun nativeRetainService(serviceHandle: Long)
    private external fun nativeReleaseService(serviceHandle: Long)

    companion object {
        init {
            System.loadLibrary("odpar_greenfield")
        }
    }
}
