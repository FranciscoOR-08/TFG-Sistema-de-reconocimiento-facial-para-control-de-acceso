import 'package:flutter/material.dart';
import 'registro_model.dart';
import 'registro_service.dart';
import 'package:intl/intl.dart';


// =====================================================
// REGISTRO LIST – Pantalla principal con historial
// =====================================================
class RegistroList extends StatefulWidget {
  const RegistroList({super.key});

  @override
  State<RegistroList> createState() => _RegistroListState();
}

class _RegistroListState extends State<RegistroList> {
  late Future<List<Registro>> _futureRegistros; // Contiene la lista de accesos (se carga asíncronamente)

  @override
  void initState() {
    super.initState();
    _futureRegistros = RegistroService.fetchRegistros(); //Al iniciar la pantalla pedimos los registros al backend
  }

  //Método para refrescar los registros manualmente (botón refresh)
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
          //Botón para volver a cargar registros
          IconButton(
            tooltip: 'Actualizar registros',
            icon: const Icon(Icons.refresh),
            onPressed: _refreshRegistros,
          ),
        ],
      ),

      // =====================================================
      // BODY – FutureBuilder que espera datos del backend
      // =====================================================
      body: FutureBuilder<List<Registro>>(
        future: _futureRegistros,
        builder: (context, snapshot) {
          //Caso 1: aún cargando datos
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }
          //Caso 2: error al obtener datos
          else if (snapshot.hasError) {
            return Center(
              child: Text(
                'Error: ${snapshot.error}',
                style: theme.textTheme.bodyLarge
                    ?.copyWith(color: theme.colorScheme.error),
                textAlign: TextAlign.center,
              ),
            );
          }
          //Caso 3: no hay datos
          else if (!snapshot.hasData || snapshot.data!.isEmpty) {
            return Center(
              child: Text(
                'No hay registros disponibles',
                style: theme.textTheme.bodyMedium
                    ?.copyWith(color: Colors.grey),
              ),
            );
          }
          //Caso 4: datos disponibles
          final registros = snapshot.data!;
          // Ordenar por más recientes primero
          registros.sort((a, b) => b.timestamp.compareTo(a.timestamp));

          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 12),
            itemCount: registros.length,
            separatorBuilder: (_, __) => const SizedBox(height: 10),
            itemBuilder: (context, index) {
              final r = registros[index];
              //Formateo de la fecha con intl
              final fecha = DateFormat('d MMMM y, HH:mm:ss', 'es_ES')
                  .format(r.timestamp);
              //Determinar si el acceso fue exitoso o fallido
              final isSuccess = r.status.toLowerCase() == 'success';

              // =====================================================
              // CARD – visualización de cada registro
              // =====================================================
              return Card(
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12)),
                elevation: 4,
                shadowColor: isSuccess
                    ? Colors.greenAccent.withOpacity(0.3)
                    : Colors.redAccent.withOpacity(0.3),
                child: ListTile(
                  contentPadding: const EdgeInsets.symmetric(
                      horizontal: 20, vertical: 14),
                  //Icono: verde si éxito, rojo si error
                  leading: Icon(
                    isSuccess
                        ? Icons.check_circle_outline
                        : Icons.error_outline,
                    color: isSuccess ? Colors.green : Colors.red,
                    size: 36,
                  ),
                  title: Text(
                    r.message,
                    style: theme.textTheme.titleMedium
                        ?.copyWith(fontWeight: FontWeight.bold),
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
                        style: theme.textTheme.bodySmall
                            ?.copyWith(color: Colors.grey[600]),
                      ),
                    ],
                  ),

                  // Estado al lado derecho (SUCCESS / ERROR)
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
