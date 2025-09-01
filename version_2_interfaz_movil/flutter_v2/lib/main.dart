//main.dart
import 'package:flutter/material.dart';
import 'registro_list.dart';
import 'package:intl/date_symbol_data_local.dart'; // <-- Importación necesaria
import 'package:flutter_localizations/flutter_localizations.dart'; // <-- Importación necesaria

void main() async {
  WidgetsFlutterBinding.ensureInitialized(); // <-- Necesario para usar 'await' en main
  await initializeDateFormatting('es_ES', null); // <-- Inicializa el locale

  runApp(const MyApp()); // <-- Ejecuta la app normalmente
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Historial de Accesos',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      supportedLocales: const [
        Locale('es', 'ES'), // <-- Idioma español soportado
      ],
      home: const RegistroList(),
    );
  }
}
