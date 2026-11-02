/// # Handy Window
///
/// This package enhances the looks of Flutter applications on Linux by providing
/// modern-looking [Handy](https://gitlab.gnome.org/GNOME/libhandy) windows with
/// rounded bottom corners.
///
/// | Handy window | Flutter window |
/// |---|---|
/// | <img src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/handy-window.png" width="430"/> | <img src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/flutter-window.png" width="400"/> |
///
/// ## Usage
///
/// Add the dependency to `pubspec.yaml`:
/// ```yaml
/// dependencies:
///   handy_window:
/// ```
///
/// In `lib/main.dart`, wrap `MaterialApp.builder` with [HandyWindowFrame]:
/// ```dart
/// MaterialApp(
///   builder: (context, child) => HandyWindowFrame(child: child!),
///   ...
/// )
/// ```
library;

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

const _windowFrameChannel = EventChannel('handy_window/window_frame');

// Clip in Flutter so GTK skips offscreen Cairo masking and glReadPixels.
class HandyWindowFrame extends StatefulWidget {
  const HandyWindowFrame({super.key, required this.child});

  final Widget child;

  @override
  State<HandyWindowFrame> createState() => _HandyWindowFrameState();
}

// Preserve backwards-compatibility with earlier WindowFrame proposals.
typedef WindowFrame = HandyWindowFrame;

class _HandyWindowFrameState extends State<HandyWindowFrame> {
  StreamSubscription<Object?>? _subscription;
  double _cornerRadius = 0;
  bool _hasRim = false;

  @override
  void initState() {
    super.initState();
    _subscription = _windowFrameChannel
        .receiveBroadcastStream()
        // Widget tests have no native runner channel; leave unclipped.
        .handleError(
          (Object _) {},
          test: (error) => error is MissingPluginException,
        )
        .listen((event) {
          if (event case {'radius': final num r, 'rim': final bool rim}) {
            final radius = r.toDouble();
            if (radius != _cornerRadius || rim != _hasRim) {
              setState(() {
                _cornerRadius = radius;
                _hasRim = rim;
              });
            }
          }
        });
  }

  @override
  void dispose() {
    unawaited(_subscription?.cancel());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    var content = widget.child;
    if (_hasRim) {
      content = DecoratedBox(
        position: DecorationPosition.foreground,
        decoration: BoxDecoration(
          borderRadius: _cornerRadius > 0
              ? BorderRadius.circular(_cornerRadius)
              : null,
          border: Border.all(
            color: const Color.fromRGBO(255, 255, 255, 0.07),
          ),
        ),
        child: content,
      );
    }
    if (_cornerRadius > 0) {
      content = ClipRRect(
        borderRadius: BorderRadius.circular(_cornerRadius),
        child: content,
      );
    }
    return content;
  }
}
