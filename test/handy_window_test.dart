import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:handy_window/handy_window.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('HandyWindowFrame', () {
    const channel = EventChannel('handy_window/window_frame');

    tearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockStreamHandler(channel, null);
    });

    testWidgets(
      'omits root ClipRRect and rim border when channel is unhandled',
      (tester) async {
        await tester.pumpWidget(
          const HandyWindowFrame(
            child: SizedBox(width: 100, height: 100),
          ),
        );
        await tester.pump();

        expect(find.byType(ClipRRect), findsNothing);
        expect(
          find.byWidgetPredicate(
            (widget) =>
                widget is DecoratedBox &&
                widget.position == DecorationPosition.foreground,
          ),
          findsNothing,
        );
      },
    );

    testWidgets(
      'updates ClipRRect and CSD rim dynamically across states',
      (tester) async {
        MockStreamHandlerEventSink? eventsSink;
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .setMockStreamHandler(
              channel,
              MockStreamHandler.inline(
                onListen: (arguments, events) {
                  eventsSink = events;
                  events.success(<String, Object>{
                    'radius': 15.0,
                    'rim': true,
                  });
                },
                onCancel: (arguments) {
                  eventsSink = null;
                },
              ),
            );

        await tester.pumpWidget(
          const HandyWindowFrame(
            child: SizedBox(width: 100, height: 100),
          ),
        );
        await tester.pump();

        final clipFinder = find.byType(ClipRRect);
        expect(clipFinder, findsOneWidget);
        expect(
          tester.widget<ClipRRect>(clipFinder).borderRadius,
          BorderRadius.circular(15),
        );

        final rimFinder = find.byWidgetPredicate(
          (widget) =>
              widget is DecoratedBox &&
              widget.position == DecorationPosition.foreground,
        );
        expect(rimFinder, findsOneWidget);

        eventsSink!.success(<String, Object>{'radius': 0.0, 'rim': false});
        await tester.pumpAndSettle();

        expect(find.byType(ClipRRect), findsNothing);
        expect(
          find.byWidgetPredicate(
            (widget) =>
                widget is DecoratedBox &&
                widget.position == DecorationPosition.foreground,
          ),
          findsNothing,
        );
      },
    );
  });
}
