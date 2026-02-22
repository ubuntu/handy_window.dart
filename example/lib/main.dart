import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher_string.dart';
import 'package:yaru/yaru.dart';

void main() {
  runApp(
    YaruTheme(
      builder: (context, yaru, child) {
        return MaterialApp(
          theme: yaru.theme,
          darkTheme: yaru.darkTheme,
          debugShowCheckedModeBanner: false,
          home: const HandyPage(),
        );
      },
    ),
  );
}

class HandyPage extends StatelessWidget {
  const HandyPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Handy Window')),
      body: const Center(
        child: ClickableLink(
          url: 'https://gnome.pages.gitlab.gnome.org/libhandy',
          child: Text(
            'gitlab.gnome.org/libhandy',
            style: TextStyle(fontSize: 28),
          ),
        ),
      ),
      bottomNavigationBar: const Row(
        children: [
          RotatedArrow(angle: math.pi / 4),
          Spacer(),
          RotatedArrow(angle: -math.pi / 4),
        ],
      ),
    );
  }
}

class ClickableLink extends StatelessWidget {
  const ClickableLink({super.key, required this.url, required this.child});

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
  const RotatedArrow({super.key, required this.angle});

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
