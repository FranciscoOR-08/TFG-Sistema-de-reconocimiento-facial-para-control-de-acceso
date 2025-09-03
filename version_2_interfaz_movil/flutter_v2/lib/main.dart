import 'package:flutter/material.dart';
import 'registro_list.dart';
import 'package:intl/date_symbol_data_local.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

// =====================================================
// MAIN – Inicialización principal de la app
// =====================================================
void main() async {
  WidgetsFlutterBinding.ensureInitialized(); // Necesario para usar 'await' en main
  await initializeDateFormatting('es_ES', null); // Inicializa la configuración regional en español (para fechas, horas)

  runApp(const MyApp()); // Arranca la aplicación con el widget raíz MyApp
}

// =====================================================
// WIDGET RAÍZ – MyApp
// =====================================================
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

      // =====================================================
      // LOCALIZACIÓN – soporte multilenguaje
      // =====================================================
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      supportedLocales: const [
        Locale('es', 'ES'), // Español como idioma principal
      ],
      home: const RegistroList(),
    );
  }
}
