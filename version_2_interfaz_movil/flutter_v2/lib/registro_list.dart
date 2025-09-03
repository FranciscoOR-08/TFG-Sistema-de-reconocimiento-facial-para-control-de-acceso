import 'package:flutter/material.dart';
import 'registro_model.dart';
import 'registro_service.dart';
import 'package:intl/intl.dart';
import 'video_screen.dart';          // 👈 Pantalla para ver el streaming en vivo
import 'enrolled_faces_page.dart';  // 👈 Pantalla con la lista de rostros enrolados

/// =====================================================
/// Widget principal que muestra el historial de accesos.
/// Incluye acceso a:
///   - Actualización de registros
///   - Pantalla de streaming en vivo
///   - Lista de rostros enrolados
/// =====================================================
class RegistroList extends StatefulWidget {
  const RegistroList({super.key});

  @override
  State<RegistroList> createState() => _RegistroListState();
}

class _RegistroListState extends State<RegistroList> {
  // Lista futura de registros obtenidos desde el backend FastAPI
  late Future<List<Registro>> _futureRegistros;

  @override
  void initState() {
    super.initState();
    // Carga inicial de registros desde el backend
    _futureRegistros = RegistroService.fetchRegistros();
  }

  /// Fuerza la recarga de registros cuando el usuario pulsa "Actualizar"
  void _refreshRegistros() {
    setState(() {
      _futureRegistros = RegistroService.fetchRegistros();
    });
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Historial de Accesos'),
        actions: [
          // Botón para refrescar registros
          IconButton(
            tooltip: 'Actualizar registros',
            icon: const Icon(Icons.refresh),
            onPressed: _refreshRegistros,
          ),
          // Botón para abrir pantalla de streaming
          IconButton(
            tooltip: 'Ver video en vivo',
            icon: const Icon(Icons.videocam),
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const VideoScreen()),
              );
            },
          ),
          // Botón para abrir la lista de rostros enrolados
          IconButton(
            tooltip: 'Rostros enrolados',
            icon: const Icon(Icons.people),
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const EnrolledFacesPage()),
              );
            },
          ),
        ],
      ),

      // ============================
      // Cuerpo: lista de registros
      // ============================
      body: FutureBuilder<List<Registro>>(
        future: _futureRegistros,
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            // Mientras carga
            return const Center(child: CircularProgressIndicator());
          } else if (snapshot.hasError) {
            // Error al obtener registros
            return Center(
              child: Text(
                'Error: ${snapshot.error}',
                style: theme.textTheme.bodyLarge
                    ?.copyWith(color: theme.colorScheme.error),
                textAlign: TextAlign.center,
              ),
            );
          } else if (!snapshot.hasData || snapshot.data!.isEmpty) {
            // Sin registros disponibles
            return Center(
              child: Text(
                'No hay registros disponibles',
                style: theme.textTheme.bodyMedium
                    ?.copyWith(color: Colors.grey),
              ),
            );
          }

          // Registros obtenidos correctamente
          final registros = snapshot.data!;
          // Ordenar por fecha descendente (más recientes primero)
          registros.sort((a, b) => b.timestamp.compareTo(a.timestamp));

          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 12),
            itemCount: registros.length,
            separatorBuilder: (_, __) => const SizedBox(height: 10),
            itemBuilder: (context, index) {
              final r = registros[index];

              // Formato de fecha con intl en español
              final fecha = DateFormat('d MMMM y, HH:mm:ss', 'es_ES')
                  .format(r.timestamp);

              // Determina si el acceso fue exitoso o no
              final isSuccess = r.status.toLowerCase() == 'success';

              return Card(
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12)),
                elevation: 4,
                shadowColor: isSuccess
                    ? Colors.greenAccent.withOpacity(0.3)
                    : Colors.redAccent.withOpacity(0.3),

                // =======================
                // Item individual registro
                // =======================
                child: ListTile(
                  contentPadding: const EdgeInsets.symmetric(
                      horizontal: 20, vertical: 14),

                  // Icono de estado
                  leading: Icon(
                    isSuccess
                        ? Icons.check_circle_outline
                        : Icons.error_outline,
                    color: isSuccess ? Colors.green : Colors.red,
                    size: 36,
                  ),

                  // Mensaje principal
                  title: Text(
                    r.message,
                    style: theme.textTheme.titleMedium
                        ?.copyWith(fontWeight: FontWeight.bold),
                  ),

                  // Subdetalles: origen + fecha
                  subtitle: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const SizedBox(height: 6),
                      Text(
                        'Origen: ${r.origin.toUpperCase()}',
                        style: theme.textTheme.bodyMedium,
                      ),
                      const SizedBox(height: 8),
                      Text(
                        fecha,
                        style: theme.textTheme.bodySmall
                            ?.copyWith(color: Colors.grey[600]),
                      ),
                    ],
                  ),

                  // Estado en texto (SUCCESS / ERROR)
                  trailing: Text(
                    r.status.toUpperCase(),
                    style: theme.textTheme.labelLarge?.copyWith(
                      color: isSuccess ? Colors.green[700] : Colors.red[700],
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  isThreeLine: true,
                ),
              );
            },
          );
        },
      ),
    );
  }
}
