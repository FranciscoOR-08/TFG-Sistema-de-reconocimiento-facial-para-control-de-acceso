import 'package:flutter/material.dart';
import 'registro_model.dart';
import 'registro_service.dart';
import 'package:intl/intl.dart';
import 'video_screen.dart';
import 'enrolled_faces_page.dart';

class RegistroList extends StatefulWidget {
  const RegistroList({super.key});

  @override
  State<RegistroList> createState() => _RegistroListState();
}

class _RegistroListState extends State<RegistroList> {
  late Future<List<Registro>> _futureRegistros;

  @override
  void initState() {
    super.initState();
    _futureRegistros = RegistroService.fetchRegistros();
  }

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
          IconButton(
            tooltip: 'Actualizar registros',
            icon: const Icon(Icons.refresh),
            onPressed: _refreshRegistros,
          ),
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
      body: FutureBuilder<List<Registro>>(
        future: _futureRegistros,
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          } else if (snapshot.hasError) {
            return Center(
              child: Text(
                'Error: ${snapshot.error}',
                style: theme.textTheme.bodyLarge?.copyWith(color: theme.colorScheme.error),
                textAlign: TextAlign.center,
              ),
            );
          } else if (!snapshot.hasData || snapshot.data!.isEmpty) {
            return Center(
              child: Text(
                'No hay registros disponibles',
                style: theme.textTheme.bodyMedium?.copyWith(color: Colors.grey),
              ),
            );
          }

          final registros = snapshot.data!;
          registros.sort((a, b) => b.timestamp.compareTo(a.timestamp));

          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 12),
            itemCount: registros.length,
            separatorBuilder: (_, __) => const SizedBox(height: 10),
            itemBuilder: (context, index) {
              final r = registros[index];
              final fecha = DateFormat('d MMMM y, HH:mm:ss', 'es_ES').format(r.timestamp);
              final isSuccess = r.status.toLowerCase() == 'success';

              return Card(
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                elevation: 4,
                shadowColor: isSuccess ? Colors.greenAccent.withOpacity(0.3) : Colors.redAccent.withOpacity(0.3),
                child: ListTile(
                  contentPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
                  leading: Icon(
                    isSuccess ? Icons.check_circle_outline : Icons.error_outline,
                    color: isSuccess ? Colors.green : Colors.red,
                    size: 36,
                  ),
                  title: Text(
                    r.message,
                    style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold),
                  ),
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
                        style: theme.textTheme.bodySmall?.copyWith(color: Colors.grey[600]),
                      ),
                    ],
                  ),
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

