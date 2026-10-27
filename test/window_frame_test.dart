import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:handy_window/handy_window.dart';

const _inset = 2;
const _atlasSize = 2 * _inset + 1;
const _radius = 2;

const _blue = [0, 0, 255, 255];
const _red = [255, 0, 0, 255];
const _green = [0, 255, 0, 255];
const _transparent = [0, 0, 0, 0];

/// Blue background, red top-edge rim, masks cutting each corner's outer pixel.
Map<String, Object?> _message({int scale = 1, bool masked = true}) {
  Uint8List atlas(List<int> Function(int x, int y) colorAt) =>
      Uint8List.fromList([
        for (var y = 0; y < _atlasSize; y++)
          for (var x = 0; x < _atlasSize; x++) ...colorAt(x, y),
      ]);

  final outermost = [
    (0, 0),
    (_radius - 1, 0),
    (0, _radius - 1),
    (_radius - 1, _radius - 1),
  ];
  return {
    'scale': scale,
    'inset': _inset,
    'background': atlas((_, _) => _blue),
    if (masked) ...{
      'rim': atlas((_, y) => y == 0 ? _red : _transparent),
      'radius': _radius,
      'masks': Uint8List.fromList([
        for (final corner in outermost)
          for (var y = 0; y < _radius; y++)
            for (var x = 0; x < _radius; x++) corner == (x, y) ? 0 : 255,
      ]),
    },
  };
}

void main() {
  group('enableWindowFrame', () {
    testWidgets('applies only the newest of overlapping styles', (
      tester,
    ) async {
      final messenger = tester.binding.defaultBinaryMessenger;
      addTearDown(
        () => messenger.setMessageHandler('handy_window/window_frame', null),
      );
      Future<void> styleChanged(Object? message) =>
          messenger.handlePlatformMessage(
            'handy_window/window_frame',
            const StandardMethodCodec().encodeMethodCall(
              MethodCall('styleChanged', message),
            ),
            null,
          );

      await tester.runAsync(() async {
        final style = await enableWindowFrame();
        // The masked style uploads three images, so it finishes decoding last.
        await Future.wait([
          styleChanged(_message()),
          styleChanged(_message(masked: false)),
        ]);
        addTearDown(() => style.value?.dispose());

        expect(style.value, isNotNull);
        expect(style.value!.rim, isNull);
      });
    });
  });

  group('WindowFrameStyle.decode', () {
    test('decodes a masked frame', () async {
      final style = (await WindowFrameStyle.decode(_message()))!;
      addTearDown(style.dispose);

      expect(style.scale, 1);
      expect(style.inset, _inset);
      expect(style.background.width, _atlasSize);
      expect(style.rim!.width, _atlasSize);
      expect(style.corners!.width, 2 * _radius);
      expect(style.corners!.height, 2 * _radius);
    });

    test('decodes an unmasked frame', () async {
      final style = (await WindowFrameStyle.decode(_message(masked: false)))!;
      addTearDown(style.dispose);

      expect(style.rim, isNull);
      expect(style.corners, isNull);
    });

    final malformed = <String, Object?>{
      'null': null,
      'a list': [1, 2],
      'a zero scale': {..._message(), 'scale': 0},
      'a fractional scale': {..._message(), 'scale': 1.5},
      'a negative inset': {..._message(), 'inset': -1},
      'a short background': {..._message(), 'background': Uint8List(4)},
      'a missing background': {..._message(), 'background': null},
      'a short rim': {..._message(), 'rim': Uint8List(4)},
      'a rim without masks': {..._message(), 'masks': null},
      'masks without a rim': {..._message(), 'rim': null},
      'a zero radius': {..._message(), 'radius': 0, 'masks': Uint8List(0)},
      'masks for another radius': {..._message(), 'radius': _radius + 1},
    };
    for (final MapEntry(key: description, value: message)
        in malformed.entries) {
      test('rejects $description', () async {
        expect(await WindowFrameStyle.decode(message), isNull);
      });
    }
  });

  group('WindowFrame', () {
    const size = Size(40, 30);
    final boundaryKey = GlobalKey();

    Future<ByteData> capture(
      WidgetTester tester, {
      double pixelRatio = 1,
    }) async {
      final boundary =
          boundaryKey.currentContext!.findRenderObject()!
              as RenderRepaintBoundary;
      return (await tester.runAsync(() async {
        final image = await boundary.toImage(pixelRatio: pixelRatio);
        final bytes = await image.toByteData();
        image.dispose();
        return bytes;
      }))!;
    }

    Future<ByteData> paintFrame(
      WidgetTester tester,
      WindowFrameStyle? style,
    ) async {
      tester.view
        ..devicePixelRatio = 1
        ..physicalSize = size;
      addTearDown(tester.view.reset);
      final notifier = ValueNotifier(style);
      addTearDown(() {
        notifier.value?.dispose();
        notifier.dispose();
      });

      await tester.pumpWidget(
        RepaintBoundary(
          key: boundaryKey,
          child: WindowFrame(
            style: notifier,
            child: const Center(
              child: SizedBox.square(
                dimension: 10,
                child: ColoredBox(color: Color(0xff00ff00)),
              ),
            ),
          ),
        ),
      );
      return capture(tester);
    }

    List<int> pixel(ByteData bytes, int x, int y, {int scale = 1}) {
      final i = (y * size.width.toInt() * scale + x) * 4;
      return [for (var c = 0; c < 4; c++) bytes.getUint8(i + c)];
    }

    testWidgets('paints the frame around the app like libhandy', (
      tester,
    ) async {
      final bytes = await paintFrame(
        tester,
        await tester.runAsync<WindowFrameStyle?>(
          () => WindowFrameStyle.decode(_message()),
        ),
      );
      const right = 39;
      const bottom = 29;

      expect(pixel(bytes, 0, 0), _transparent);
      expect(pixel(bytes, right, 0), _transparent);
      expect(pixel(bytes, 0, bottom), _transparent);
      expect(pixel(bytes, right, bottom), _transparent);
      expect(pixel(bytes, 1, 0), _red);
      expect(pixel(bytes, 20, 0), _red);
      expect(pixel(bytes, right - 1, 0), _red);
      expect(pixel(bytes, 1, 1), _blue);
      expect(pixel(bytes, 5, 15), _blue);
      expect(pixel(bytes, right, 15), _blue);
      expect(pixel(bytes, 20, 15), _green);
    });

    // As when the window moves to a monitor with twice the scale.
    Future<ByteData> doubleScale(WidgetTester tester) async {
      tester.view
        ..devicePixelRatio = 2
        ..physicalSize = size * 2;
      await tester.pump();
      return capture(tester, pixelRatio: 2);
    }

    testWidgets('paints the frame for a new scale', (tester) async {
      await paintFrame(
        tester,
        await tester.runAsync<WindowFrameStyle?>(
          () => WindowFrameStyle.decode(_message(scale: 2)),
        ),
      );
      final bytes = await doubleScale(tester);

      expect(pixel(bytes, 0, 0, scale: 2), _transparent);
      expect(pixel(bytes, 1, 0, scale: 2), _red);
      expect(pixel(bytes, 1, 1, scale: 2), _blue);
      expect(pixel(bytes, 40, 30, scale: 2), _green);
    });

    testWidgets("paints the old scale's frame until the new one arrives", (
      tester,
    ) async {
      await paintFrame(
        tester,
        await tester.runAsync<WindowFrameStyle?>(
          () => WindowFrameStyle.decode(_message()),
        ),
      );
      final bytes = await doubleScale(tester);

      // Each pixel of the style covers two by two device pixels.
      expect(pixel(bytes, 1, 1, scale: 2), _transparent);
      expect(pixel(bytes, 2, 1, scale: 2), _red);
      expect(pixel(bytes, 2, 2, scale: 2), _blue);
      expect(pixel(bytes, 40, 30, scale: 2), _green);
    });

    testWidgets('paints nothing without a style', (tester) async {
      final bytes = await paintFrame(tester, null);

      expect(pixel(bytes, 1, 0), _transparent);
      expect(pixel(bytes, 20, 15), _green);
    });
  });
}
