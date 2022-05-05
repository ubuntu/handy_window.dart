import 'package:flutter/material.dart';
import 'package:screen_retriever/screen_retriever.dart';
import 'package:url_launcher/url_launcher_string.dart';
import 'package:window_manager/window_manager.dart';
import 'package:yaru/yaru.dart';
import 'package:yaru_icons/yaru_icons.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // Must add this line.
  await windowManager.ensureInitialized();

  WindowOptions windowOptions = WindowOptions(
    size: const Size(800, 600),
    skipTaskbar: false,
  );
  windowManager.waitUntilReadyToShow(windowOptions, () async {
    await windowManager.show();
    await windowManager.focus();
  });
  runApp(
    MaterialApp(
      theme: yaruLight,
      darkTheme: yaruDark,
      home: const HandyPage(),
      debugShowCheckedModeBanner: false,
    ),
  );
}

class HandyPage extends StatelessWidget {
  const HandyPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: YaruAppBar(
          title: const Text('Flutter AppBar'),
          appBarHeight: Theme.of(context).appBarTheme.toolbarHeight ?? 48),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: const [
            ClickableLink(
              url:
                  'https://github.com/ubuntu-flutter-community/handy_window/tree/import',
              child: Text(
                'handy_window',
                style: TextStyle(fontSize: 28),
              ),
            ),
            ClickableLink(
              url: 'https://github.com/ubuntu/yaru_widgets.dart',
              child: Text(
                'yaru_widgets.dart',
                style: TextStyle(fontSize: 28),
              ),
            ),
            ClickableLink(
              url: 'https://github.com/ubuntu/yaru.dart',
              child: Text(
                'yaru.dart',
                style: TextStyle(fontSize: 28),
              ),
            )
          ],
        ),
      ),
    );
  }
}

final screenRetriever = ScreenRetriever.instance;

class YaruAppBar extends StatefulWidget implements PreferredSizeWidget {
  final double appBarHeight;
  final Widget title;

  const YaruAppBar({Key? key, required this.appBarHeight, required this.title})
      : super(key: key);

  @override
  State<YaruAppBar> createState() => _YaruAppBarState();

  @override
  Size get preferredSize => Size(0, appBarHeight);
}

class _YaruAppBarState extends State<YaruAppBar>
    with WindowListener, ScreenListener {
  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      // onLongPress: () async => await WindowManager.instance
      //     .setPosition(await screenRetriever.getCursorScreenPoint()),
      onLongPressEnd: (_) async => await WindowManager.instance
          .setPosition(await screenRetriever.getCursorScreenPoint()),
      onDoubleTap: () async => await WindowManager.instance.isMaximized()
          ? await WindowManager.instance.unmaximize()
          : await WindowManager.instance.maximize(),
      child: AppBar(
        title: widget.title,
        backgroundColor: const Color.fromARGB(255, 236, 236, 236),
        actions: const [
          TitleButton(
            iconData: YaruIcons.window_minimize,
            titleButtonAction: TitleButtonAction.minimize,
          ),
          TitleButton(
              iconData: YaruIcons.window_maximize,
              titleButtonAction: TitleButtonAction.maximize),
          TitleButton(
            iconData: YaruIcons.window_close,
            titleButtonAction: TitleButtonAction.close,
          ),
        ],
      ),
    );
  }

  @override
  void initState() {
    screenRetriever.addListener(this);
    windowManager.addListener(this);
    super.initState();
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    super.dispose();
  }
}

class TitleButton extends StatefulWidget {
  const TitleButton({
    Key? key,
    required this.iconData,
    required this.titleButtonAction,
  }) : super(key: key);

  final IconData iconData;
  final TitleButtonAction titleButtonAction;

  @override
  State<TitleButton> createState() => _TitleButtonState();
}

class _TitleButtonState extends State<TitleButton> {
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(9.0),
      child: SizedBox(
        height: 30,
        width: 30,
        child: Material(
          color: const Color.fromARGB(255, 223, 223, 223),
          borderRadius: BorderRadius.circular(100),
          child: InkWell(
            borderRadius: BorderRadius.circular(100),
            onTap: () async {
              switch (widget.titleButtonAction) {
                case TitleButtonAction.close:
                  await close();
                  break;
                case TitleButtonAction.maximize:
                  await maximize();
                  break;
                case TitleButtonAction.minimize:
                  await minimize();
                  break;
                default:
              }
            },
            child: Icon(
              widget.iconData,
              size: 20,
            ),
          ),
        ),
      ),
    );
  }
}

enum TitleButtonAction { close, minimize, maximize }

Future<void> close() async => await WindowManager.instance.close();

Future<void> maximize() async => await WindowManager.instance.isMaximized()
    ? await WindowManager.instance.unmaximize()
    : await WindowManager.instance.maximize();

Future<void> minimize() async => await WindowManager.instance.minimize();

class ClickableLink extends StatelessWidget {
  const ClickableLink({
    Key? key,
    required this.url,
    required this.child,
  }) : super(key: key);

  final String url;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () => launchUrlString(url),
        child: DefaultTextStyle(
          style: TextStyle(
            color: Colors.blue.shade700,
            decoration: TextDecoration.underline,
          ),
          child: child,
        ),
      ),
    );
  }
}

class RotatedArrow extends StatelessWidget {
  const RotatedArrow({Key? key, required this.angle}) : super(key: key);

  final double angle;

  @override
  Widget build(BuildContext context) {
    return Transform.rotate(
      angle: angle,
      child: Icon(
        Icons.arrow_downward,
        color: Theme.of(context).primaryColor,
        size: 72,
      ),
    );
  }
}
