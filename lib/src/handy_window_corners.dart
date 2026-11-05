import 'dart:ui' as ui;

import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Rounds the corners of the Handy window and draws its outline in Flutter.
///
/// Without it, libhandy rounds the corners by copying every frame from the
/// GPU to mask them, which makes scrolling and animations slow with themes
/// that have rounded window corners, such as Yaru.
///
/// Use it once, around the whole app:
///
/// ```dart
/// void main() {
///   runApp(const HandyWindowCorners(child: MyApp()));
/// }
/// ```
///
/// The corners follow the window: the radius comes from the GTK theme, and
/// maximized and fullscreen windows get square corners and no outline. In a
/// window with a GTK header bar, and on other platforms, it shows [child] as
/// it is and libhandy keeps rounding the corners.
class HandyWindowCorners extends StatefulWidget {
  /// Creates a widget that rounds the window corners around [child].
  const HandyWindowCorners({super.key, required this.child});

  /// The app.
  final Widget child;

  @override
  State<HandyWindowCorners> createState() => _HandyWindowCornersState();
}

class _HandyWindowCornersState extends State<HandyWindowCorners> {
  static const _channel = MethodChannel('handy_window');
  // The instance that owns the channel. A replacement widget must not have
  // the channel, or the corners, taken back by the instance it replaces.
  static _HandyWindowCornersState? _active;

  var _radius = 0;
  var _outline = false;
  // Counts cornersChanged calls, so a getCorners reply taken before one is
  // dropped instead of overriding the newer corners.
  var _cornerChanges = 0;

  @override
  void initState() {
    super.initState();
    _active = this;
    _channel.setMethodCallHandler(_handleMethodCall);
    _getCorners();
  }

  @override
  void dispose() {
    if (_active == this) {
      _active = null;
      _channel.setMethodCallHandler(null);
      _reportDrawn(null);
    }
    super.dispose();
  }

  Future<void> _getCorners() async {
    final changes = _cornerChanges;
    try {
      final corners = (await _channel.invokeMapMethod<String, Object?>(
        'getCorners',
      ))!;
      if (changes == _cornerChanges) _update(corners);
    } on MissingPluginException {
      // Not a supported window: libhandy keeps rounding the corners.
    } on PlatformException {
      // A failed query leaves libhandy masking the corners, as above.
    }
  }

  Future<void> _handleMethodCall(MethodCall call) async {
    final arguments = call.arguments;
    if (call.method != 'cornersChanged' ||
        arguments is! Map<Object?, Object?>) {
      return;
    }
    if (_update(arguments)) _cornerChanges++;
  }

  bool _update(Map<Object?, Object?> corners) {
    if (!mounted || _active != this) return false;
    final radius = corners['radius'];
    final outline = corners['outline'];
    if (radius is! int || outline is! bool) return false;
    assert(radius > 0 || !outline, 'The outline comes with rounded corners.');
    setState(() {
      _radius = radius;
      _outline = outline;
    });
    _reportWhenShown(radius);
    return true;
  }

  Future<void> _reportWhenShown(int radius) async {
    // The frame with the new corners and two more, so that it is on screen
    // before the window stops masking the corners.
    for (var i = 0; i < 3; i++) {
      await SchedulerBinding.instance.endOfFrame;
    }
    if (_active == this && mounted && radius == _radius) {
      await _reportDrawn(radius);
    }
  }

  /// Tells the window which radius Flutter draws, or null to hand the corners
  /// back to libhandy.
  Future<void> _reportDrawn(int? radius) async {
    try {
      await _channel.invokeMethod<void>('cornersDrawn', radius);
    } on MissingPluginException {
      // Not a supported window, see _getCorners.
    } on PlatformException {
      // Not a supported window, see _getCorners.
    }
  }

  @override
  Widget build(BuildContext context) {
    // Always a CustomPaint, so that corner changes keep the app's state.
    return CustomPaint(
      foregroundPainter: _radius > 0
          ? _CornersPainter(
              radius: _radius.toDouble(),
              outline: _outline,
              devicePixelRatio: MediaQuery.devicePixelRatioOf(context),
            )
          : null,
      child: widget.child,
    );
  }
}

// `decoration-overlay` in assets/handy-window.scss: 1px of white at 7%.
const _outlineColor = Color(0x12FFFFFF);

/// Erases the corners outside the arcs and draws the outline over the app.
///
/// Impeller does not anti-alias clips on Linux, so each arc is a radial
/// gradient with a ramp one device pixel wide, like libhandy's cairo masks.
class _CornersPainter extends CustomPainter {
  const _CornersPainter({
    required this.radius,
    required this.outline,
    required this.devicePixelRatio,
  });

  final double radius;
  final bool outline;
  final double devicePixelRatio;

  @override
  void paint(Canvas canvas, Size size) {
    final r = radius;
    final w = size.width;
    final h = size.height;
    final halfPixel = 0.5 / devicePixelRatio;
    // libhandy's masks: circles half a device pixel smaller than the radius
    // that touch the window edges, see create_masks() in hdy-window-mixin.c.
    final maskRadius = r - halfPixel;
    final ringEnd = r + halfPixel;
    double ringStop(double distance) => (distance / ringEnd).clamp(0.0, 1.0);

    for (final (corner, inward) in [
      (Offset.zero, const Offset(1, 1)),
      (Offset(w, 0), const Offset(-1, 1)),
      (Offset(0, h), const Offset(1, -1)),
      (Offset(w, h), const Offset(-1, -1)),
    ]) {
      final box = Rect.fromPoints(corner, corner + inward * r);
      if (outline) {
        canvas.drawRect(
          box,
          Paint()
            ..shader = ui.Gradient.radial(
              corner + inward * r,
              ringEnd,
              const [
                Color(0x00FFFFFF),
                _outlineColor,
                _outlineColor,
                Color(0x00FFFFFF),
              ],
              [
                ringStop(r - 1 - halfPixel),
                ringStop(r - 1 + halfPixel),
                ringStop(r - halfPixel),
                1,
              ],
            ),
        );
      }
      // Erases outside the mask circle, outline included like in libhandy.
      canvas.drawRect(
        box,
        Paint()
          ..blendMode = BlendMode.dstOut
          ..shader = ui.Gradient.radial(
            corner + inward * maskRadius,
            maskRadius + halfPixel,
            const [Color(0x00000000), Color(0xFF000000)],
            [(maskRadius - halfPixel) / (maskRadius + halfPixel), 1],
          ),
      );
    }

    if (outline) {
      final paint = Paint()..color = _outlineColor;
      canvas
        ..drawRect(Rect.fromLTRB(r, 0, w - r, 1), paint)
        ..drawRect(Rect.fromLTRB(r, h - 1, w - r, h), paint)
        ..drawRect(Rect.fromLTRB(0, r, 1, h - r), paint)
        ..drawRect(Rect.fromLTRB(w - 1, r, w, h - r), paint);
    }
  }

  @override
  bool shouldRepaint(_CornersPainter oldDelegate) =>
      radius != oldDelegate.radius ||
      outline != oldDelegate.outline ||
      devicePixelRatio != oldDelegate.devicePixelRatio;
}
