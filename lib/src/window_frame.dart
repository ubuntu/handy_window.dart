import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

const _channel = MethodChannel('handy_window/window_frame');

/// Call once, before [runApp]. Stays null while libhandy keeps the corners.
Future<ValueListenable<WindowFrameStyle?>> enableWindowFrame() async {
  final style = ValueNotifier<WindowFrameStyle?>(null);
  // Decoding waits on the GPU, so styles can finish out of order.
  var updates = 0;

  Future<void> update(Object? message) async {
    final generation = ++updates;
    WindowFrameStyle? decoded;
    Exception? uploadError;
    StackTrace? uploadStack;
    try {
      decoded = await WindowFrameStyle.decode(message);
    } on Exception catch (error, stack) {
      uploadError = error;
      uploadStack = stack;
    }
    if (generation != updates) {
      decoded?.dispose();
      return;
    }
    if (message != null && decoded == null) {
      // The plugin sends only valid styles, so only a GPU error gets here.
      assert(uploadError != null, 'Undecodable window frame style');
      FlutterError.reportError(
        FlutterErrorDetails(
          exception: uploadError ?? 'Undecodable window frame style',
          stack: uploadStack,
          library: 'handy_window',
          context: ErrorDescription(
            'while uploading the window frame; libhandy keeps painting the '
            'window corners',
          ),
        ),
      );
      await _channel.invokeMethod<void>('disable');
    }
    final previous = style.value;
    style.value = decoded;
    previous?.dispose();
  }

  _channel.setMethodCallHandler((call) async {
    if (call.method == 'styleChanged') await update(call.arguments);
  });
  try {
    if (!await _blendsExactly()) {
      debugPrint(
        'handy_window: Flutter blends differently from cairo; libhandy keeps '
        'painting the window corners',
      );
      return style;
    }
  } on Exception catch (error, stack) {
    FlutterError.reportError(
      FlutterErrorDetails(
        exception: error,
        stack: stack,
        library: 'handy_window',
        context: ErrorDescription(
          'while checking how Flutter blends; libhandy keeps painting the '
          'window corners',
        ),
      ),
    );
    return style;
  }
  try {
    await update(await _channel.invokeMethod<Object?>('enable'));
  } on MissingPluginException {
    // No Handy window to take over, e.g. without client-side decorations.
  }
  return style;
}

/// The listenable from [enableWindowFrame] owns and disposes these.
class WindowFrameStyle {
  WindowFrameStyle._({
    required this.scale,
    required this.inset,
    required this.background,
    this.rim,
    this.corners,
  });

  /// Null if [message] is malformed; throws if the GPU refuses the images.
  @visibleForTesting
  static Future<WindowFrameStyle?> decode(Object? message) async {
    if (message is! Map) return null;
    final scale = message['scale'];
    final inset = message['inset'];
    final background = message['background'];
    if (scale is! int ||
        scale < 1 ||
        inset is! int ||
        inset < 0 ||
        background is! Uint8List) {
      return null;
    }
    final atlasSize = 2 * inset + 1;
    if (background.length != atlasSize * atlasSize * 4) return null;

    final rim = message['rim'];
    final radius = message['radius'];
    final masks = message['masks'];
    if (rim == null && radius == null && masks == null) {
      return WindowFrameStyle._(
        scale: scale,
        inset: inset,
        background: await _image(background, atlasSize, atlasSize),
      );
    }
    if (rim is! Uint8List ||
        rim.length != background.length ||
        radius is! int ||
        radius < 1 ||
        masks is! Uint8List ||
        masks.length != 4 * radius * radius) {
      return null;
    }
    return WindowFrameStyle._(
      scale: scale,
      inset: inset,
      background: await _image(background, atlasSize, atlasSize),
      rim: await _image(rim, atlasSize, atlasSize),
      corners: await _cornersImage(masks, radius),
    );
  }

  /// The device pixel ratio the images are rendered for.
  final int scale;

  /// Corner size of the nine-slice [background] and [rim], in image pixels.
  final int inset;

  /// The window node's background and frame, drawn under the app.
  final ui.Image background;

  /// libhandy's rim over the app. Null, like [corners], when it doesn't mask.
  final ui.Image? rim;

  /// libhandy's corner masks, as the four quadrants of one image.
  final ui.Image? corners;

  void dispose() {
    background.dispose();
    rim?.dispose();
    corners?.dispose();
  }
}

/// Must be the root widget, so it covers the whole Flutter view.
class WindowFrame extends SingleChildRenderObjectWidget {
  const WindowFrame({required this.style, super.key, super.child});

  /// The style from [enableWindowFrame].
  final ValueListenable<WindowFrameStyle?> style;

  @override
  RenderWindowFrame createRenderObject(BuildContext context) {
    return RenderWindowFrame(style: style);
  }

  @override
  void updateRenderObject(
    BuildContext context,
    RenderWindowFrame renderObject,
  ) {
    renderObject.style = style;
  }
}

class RenderWindowFrame extends RenderProxyBox {
  RenderWindowFrame({required ValueListenable<WindowFrameStyle?> style})
    : _style = style;

  ValueListenable<WindowFrameStyle?> _style;
  set style(ValueListenable<WindowFrameStyle?> value) {
    if (value == _style) return;
    if (attached) {
      _style.removeListener(markNeedsPaint);
      value.addListener(markNeedsPaint);
    }
    _style = value;
    markNeedsPaint();
  }

  @override
  void attach(PipelineOwner owner) {
    super.attach(owner);
    _style.addListener(markNeedsPaint);
  }

  @override
  void detach() {
    _style.removeListener(markNeedsPaint);
    super.detach();
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    super.paint(context, offset);
    final style = _style.value;
    if (style == null) return;
    // Draw at the style's scale, not the view's: it lags behind scale changes.
    final width = (size.width * style.scale).roundToDouble();
    final height = (size.height * style.scale).roundToDouble();
    final bounds = Rect.fromLTWH(0, 0, width, height);
    final center = Rect.fromLTWH(
      style.inset.toDouble(),
      style.inset.toDouble(),
      1,
      1,
    );
    final canvas = context.canvas
      ..save()
      ..translate(offset.dx, offset.dy)
      ..scale(1 / style.scale);
    // libhandy's order: background under the app, rim over both, then masks.
    canvas.drawImageNine(
      style.background,
      center,
      bounds,
      _paint(BlendMode.dstOver),
    );
    if (style.rim case final rim?) {
      canvas.drawImageNine(rim, center, bounds, _paint(BlendMode.srcOver));
    }
    if (style.corners case final corners?) {
      final radius = corners.width / 2;
      final paint = _paint(BlendMode.dstIn);
      for (final (column, row) in const [(0, 0), (1, 0), (0, 1), (1, 1)]) {
        canvas.drawImageRect(
          corners,
          Rect.fromLTWH(column * radius, row * radius, radius, radius),
          Rect.fromLTWH(
            column == 0 ? 0 : width - radius,
            row == 0 ? 0 : height - radius,
            radius,
            radius,
          ),
          paint,
        );
      }
    }
    canvas.restore();
    // Raster caching this picture would break its blending with the app below.
    context.setWillChangeHint();
  }
}

Paint _paint(BlendMode blendMode) => Paint()
  ..blendMode = blendMode
  ..filterQuality = FilterQuality.none
  ..isAntiAlias = false;

// pixman's x * y / 255, correctly rounded, like mul() in linux/window_frame.cc.
int _mul(int x, int y) {
  final t = x * y + 0x80;
  return ((t >> 8) + t) >> 8;
}

/// Whether Flutter blends exactly like pixman, so the corners match libhandy's.
Future<bool> _blendsExactly() async {
  const size = 16;
  const inset = 3;
  const atlasSize = 2 * inset + 1;
  final random = math.Random(2080);
  List<int> pixel(int index) {
    final alpha = switch (index) {
      0 => 0,
      1 => 255,
      2 => 1,
      3 => 254,
      4 => 128,
      _ => random.nextInt(256),
    };
    return [for (var i = 0; i < 3; i++) random.nextInt(alpha + 1), alpha];
  }

  final sources = [for (var i = 0; i < size * size; i++) pixel(i)];
  final destinations = [
    for (var i = 0; i < size * size; i++) pixel(i),
  ].reversed.toList();
  Future<ui.Image> imageOf(List<List<int>> pixels, int width) => _image(
    Uint8List.fromList([for (final pixel in pixels) ...pixel]),
    width,
    width,
  );
  final source = await imageOf(sources, size);
  final atlas = await imageOf(
    sources.sublist(0, atlasSize * atlasSize),
    atlasSize,
  );
  final destination = await imageOf(destinations, size);
  final checks = <BlendMode?, int Function(int s, int d, int sa, int da)>{
    // No blend mode: the images alone must come back unchanged.
    null: (s, d, sa, da) => d,
    BlendMode.srcOver: (s, d, sa, da) => s + _mul(d, 255 - sa),
    BlendMode.dstOver: (s, d, sa, da) => d + _mul(s, 255 - da),
    BlendMode.dstIn: (s, d, sa, da) => _mul(d, sa),
  };
  // Drawn like paint() does: some renderers blend a stretched nine-slice
  // inexactly, though they blend the same image drawn 1:1 exactly.
  const nineSliced = {BlendMode.srcOver, BlendMode.dstOver};
  int fromAtlas(int i) => i < inset
      ? i
      : i >= size - inset
      ? i - size + atlasSize
      : inset;
  try {
    for (final MapEntry(key: blendMode, value: expected) in checks.entries) {
      final recorder = ui.PictureRecorder();
      final canvas = Canvas(recorder);
      final bounds = Rect.fromLTWH(0, 0, size.toDouble(), size.toDouble());
      canvas.drawImageRect(destination, bounds, bounds, _paint(BlendMode.src));
      if (nineSliced.contains(blendMode)) {
        final center = Rect.fromLTWH(inset.toDouble(), inset.toDouble(), 1, 1);
        canvas.drawImageNine(atlas, center, bounds, _paint(blendMode!));
      } else if (blendMode != null) {
        canvas.drawImageRect(source, bounds, bounds, _paint(blendMode));
      }
      final picture = recorder.endRecording();
      final image = await picture.toImage(size, size);
      picture.dispose();
      final bytes = await image.toByteData();
      image.dispose();
      if (bytes == null) return false;
      for (var i = 0; i < size * size; i++) {
        final s = nineSliced.contains(blendMode)
            ? sources[fromAtlas(i ~/ size) * atlasSize + fromAtlas(i % size)]
            : sources[i];
        final d = destinations[i];
        for (var channel = 0; channel < 4; channel++) {
          final actual = bytes.getUint8(i * 4 + channel);
          if (actual != expected(s[channel], d[channel], s[3], d[3])) {
            return false;
          }
        }
      }
    }
    return true;
  } finally {
    source.dispose();
    atlas.dispose();
    destination.dispose();
  }
}

/// [masks] holds the four corners in reading order, one per quadrant.
Future<ui.Image> _cornersImage(Uint8List masks, int radius) {
  final size = 2 * radius;
  // Black, so that each pixel is premultiplied whatever its alpha.
  final pixels = Uint8List(size * size * 4);
  for (var y = 0; y < size; y++) {
    for (var x = 0; x < size; x++) {
      final corner = y ~/ radius * 2 + x ~/ radius;
      pixels[(y * size + x) * 4 + 3] =
          masks[(corner * radius + y % radius) * radius + x % radius];
    }
  }
  return _image(pixels, size, size);
}

/// Not via [Color]s: their straight alpha leaves channels one off on NVIDIA.
Future<ui.Image> _image(
  Uint8List premultipliedRgba,
  int width,
  int height,
) async {
  final buffer = await ui.ImmutableBuffer.fromUint8List(premultipliedRgba);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: width,
    height: height,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final frame = await codec.getNextFrame();
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return frame.image;
}
