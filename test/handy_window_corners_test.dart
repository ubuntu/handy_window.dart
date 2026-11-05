import 'dart:async';

import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:handy_window/handy_window.dart';

const _channel = MethodChannel('handy_window');
const _windowKey = Key('window');

void main() {
  testWidgets('rounds and anti-aliases every corner', (tester) async {
    _mockWindow(tester, radius: 15, scale: 2);
    await tester.pumpWidget(const _Window());
    await tester.pump();

    final pixels = await _capture(tester);
    expect(pixels.alpha(0, 0), 0);
    expect(pixels.alpha(399, 0), 0);
    expect(pixels.alpha(0, 199), 0);
    expect(pixels.alpha(399, 199), 0);
    expect(pixels.alpha(200, 100), 255);
    // libhandy's mask circle covers 26-30% of this device pixel on the 45°
    // line.
    expect(pixels.alpha(8, 8), closeTo(72, 15));
  });

  testWidgets('keeps the app state when the corners change', (tester) async {
    _mockWindow(tester, radius: 15);
    await tester.pumpWidget(const _Window(child: _Stateful()));
    await _pumpFrames(tester);
    final state = tester.state(find.byType(_Stateful));

    await _pushCorners(tester, radius: 0, outline: false);
    await _pumpFrames(tester);

    expect(tester.state(find.byType(_Stateful)), same(state));
  });

  testWidgets('reports the radius once drawn and hands it back when removed', (
    tester,
  ) async {
    final drawn = _mockWindow(tester, radius: 15);
    await tester.pumpWidget(const _Window());
    await tester.pump();
    expect(drawn, isEmpty);

    await _pumpFrames(tester);
    expect(drawn, [15]);

    await tester.pumpWidget(const SizedBox());
    await tester.pump();
    expect(drawn, [15, null]);
  });

  testWidgets('keeps the newest corners when changes overtake getCorners', (
    tester,
  ) async {
    final reply = Completer<Map<Object?, Object?>?>();
    final drawn = _mockPendingWindow(tester, reply);

    await tester.pumpWidget(const _Window());
    await _pushCorners(tester, radius: 30, outline: false);
    await _pumpFrames(tester);

    reply.complete({'radius': 15, 'outline': false});
    await _pumpFrames(tester);

    expect((await _capture(tester)).alpha(0, 0), 0);
    expect(drawn, isNot(contains(15)));
    expect(drawn.last, 30);
  });

  testWidgets('keeps a getCorners reply when a corner change is malformed', (
    tester,
  ) async {
    final reply = Completer<Map<Object?, Object?>?>();
    final drawn = _mockPendingWindow(tester, reply);

    await tester.pumpWidget(const _Window());
    await tester.binding.defaultBinaryMessenger.handlePlatformMessage(
      _channel.name,
      _channel.codec.encodeMethodCall(
        const MethodCall('cornersChanged', {'radius': 15}),
      ),
      (_) {},
    );
    reply.complete({'radius': 15, 'outline': false});
    await _pumpFrames(tester);

    expect((await _capture(tester)).alpha(0, 0), 0);
    expect(drawn, [15]);
  });

  testWidgets(
    'keeps the corners and the handover when the widget is replaced',
    (tester) async {
      final drawn = _mockWindow(tester, radius: 15);
      await tester.pumpWidget(
        const KeyedSubtree(key: ValueKey('a'), child: _Window()),
      );
      await _pumpFrames(tester);
      expect(drawn, [15]);

      await tester.pumpWidget(
        const KeyedSubtree(key: ValueKey('b'), child: _Window()),
      );
      await _pumpFrames(tester);
      expect(drawn, [15, 15]);

      // The replaced instance must not have taken the channel with it.
      await _pushCorners(tester, radius: 20, outline: false);
      await _pumpFrames(tester);
      expect(drawn.last, 20);
      expect((await _capture(tester)).alpha(0, 0), 0);

      await tester.pumpWidget(const SizedBox());
      await _pumpFrames(tester);
      expect(drawn, [15, 15, 20, null]);
    },
  );

  testWidgets('keeps the corners over a composited translucent child', (
    tester,
  ) async {
    _mockWindow(tester, radius: 15);
    await tester.pumpWidget(
      const _Window(
        // A separate layer holding half-opaque white: erasing only the window
        // behind it would leave the corner at alpha 127.
        child: RepaintBoundary(
          child: Opacity(
            opacity: 0.5,
            child: ColoredBox(color: Color(0xFFFFFFFF)),
          ),
        ),
      ),
    );
    await tester.pump();

    final pixels = await _capture(tester);
    expect(pixels.alpha(0, 0), 0);
    expect(pixels.rgba(100, 50)[0], closeTo(128, 2));
  });

  testWidgets('draws the window outline only when the window has one', (
    tester,
  ) async {
    _mockWindow(tester, radius: 15, outline: true);
    await tester.pumpWidget(const _Window());
    await _pumpFrames(tester);

    var pixels = await _capture(tester);
    expect(pixels.rgba(0, 50), [18, 18, 18, 255]);
    expect(pixels.rgba(1, 50), [0, 0, 0, 255]);

    await _pushCorners(tester, radius: 15, outline: false);
    await _pumpFrames(tester);

    pixels = await _capture(tester);
    expect(pixels.rgba(0, 50), [0, 0, 0, 255]);
    expect(pixels.rgba(100, 0), [0, 0, 0, 255]);
  });

  testWidgets('keeps the window when the corners are missing or fail', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetDevicePixelRatio);
    final messenger = tester.binding.defaultBinaryMessenger;
    addTearDown(() => messenger.setMockMethodCallHandler(_channel, null));

    messenger.setMockMethodCallHandler(_channel, null);
    await tester.pumpWidget(const _Window());
    await _pumpFrames(tester);

    var pixels = await _capture(tester);
    expect(tester.takeException(), isNull);
    expect(pixels.alpha(0, 0), 255);
    expect(pixels.alpha(100, 50), 255);

    messenger.setMockMethodCallHandler(_channel, (call) async {
      throw PlatformException(code: 'failed');
    });
    await tester.pumpWidget(
      const KeyedSubtree(key: ValueKey('failing'), child: _Window()),
    );
    await _pumpFrames(tester);

    pixels = await _capture(tester);
    expect(tester.takeException(), isNull);
    expect(pixels.alpha(0, 0), 255);

    messenger.setMockMethodCallHandler(_channel, (call) async {
      if (call.method == 'getCorners') {
        return {'radius': 15, 'outline': false};
      }
      throw PlatformException(code: 'failed');
    });
    await tester.pumpWidget(
      const KeyedSubtree(key: ValueKey('report'), child: _Window()),
    );
    await _pumpFrames(tester);

    expect(tester.takeException(), isNull);
    expect((await _capture(tester)).alpha(0, 0), 0);
  });
}

/// Answers `getCorners` only when [reply] completes, so that a corner change
/// can overtake it; returns the radii reported with `cornersDrawn`.
List<int?> _mockPendingWindow(
  WidgetTester tester,
  Completer<Map<Object?, Object?>?> reply,
) {
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetDevicePixelRatio);
  final drawn = <int?>[];
  final messenger = tester.binding.defaultBinaryMessenger;
  messenger.setMockMethodCallHandler(_channel, (call) async {
    switch (call.method) {
      case 'getCorners':
        return await reply.future;
      case 'cornersDrawn':
        drawn.add(call.arguments as int?);
        return null;
    }
    throw MissingPluginException();
  });
  addTearDown(() => messenger.setMockMethodCallHandler(_channel, null));
  return drawn;
}

/// Answers `getCorners` like the native plugin for a window with the given
/// scale factor, and returns the radii that the widget reports with
/// `cornersDrawn`.
List<int?> _mockWindow(
  WidgetTester tester, {
  int radius = 0,
  bool outline = false,
  double scale = 1,
}) {
  tester.view.devicePixelRatio = scale;
  addTearDown(tester.view.resetDevicePixelRatio);
  final drawn = <int?>[];
  final messenger = tester.binding.defaultBinaryMessenger;
  messenger.setMockMethodCallHandler(_channel, (call) async {
    switch (call.method) {
      case 'getCorners':
        return {'radius': radius, 'outline': outline};
      case 'cornersDrawn':
        drawn.add(call.arguments as int?);
        return null;
    }
    throw MissingPluginException();
  });
  addTearDown(() => messenger.setMockMethodCallHandler(_channel, null));
  return drawn;
}

Future<void> _pushCorners(
  WidgetTester tester, {
  required int radius,
  required bool outline,
}) {
  return tester.binding.defaultBinaryMessenger.handlePlatformMessage(
    _channel.name,
    _channel.codec.encodeMethodCall(
      MethodCall('cornersChanged', {'radius': radius, 'outline': outline}),
    ),
    (_) {},
  );
}

Future<void> _pumpFrames(WidgetTester tester) async {
  for (var i = 0; i < 5; i++) {
    await tester.pump();
  }
}

Future<_Pixels> _capture(WidgetTester tester) async {
  final boundary = tester.renderObject<RenderRepaintBoundary>(
    find.byKey(_windowKey),
  );
  final pixels = await tester.runAsync(() async {
    final image = await boundary.toImage(
      pixelRatio: tester.view.devicePixelRatio,
    );
    final bytes = await image.toByteData();
    image.dispose();
    return _Pixels(
      image.width,
      bytes!.buffer.asUint8List(bytes.offsetInBytes, bytes.lengthInBytes),
    );
  });
  return pixels!;
}

class _Pixels {
  _Pixels(this.width, this.bytes);

  final int width;
  final Uint8List bytes;

  List<int> rgba(int x, int y) {
    final offset = (y * width + x) * 4;
    return bytes.sublist(offset, offset + 4);
  }

  int alpha(int x, int y) => rgba(x, y)[3];
}

/// A 200x100 opaque black window content.
class _Window extends StatelessWidget {
  const _Window({this.child});

  final Widget? child;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: RepaintBoundary(
        key: _windowKey,
        child: HandyWindowCorners(
          child: SizedBox(
            width: 200,
            height: 100,
            child: ColoredBox(color: const Color(0xFF000000), child: child),
          ),
        ),
      ),
    );
  }
}

class _Stateful extends StatefulWidget {
  const _Stateful();

  @override
  State<_Stateful> createState() => _StatefulState();
}

class _StatefulState extends State<_Stateful> {
  @override
  Widget build(BuildContext context) => const SizedBox.expand();
}
