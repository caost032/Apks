import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'src/engine/game_runtime.dart';
import 'src/input/input_router.dart';
import 'src/ui/design_system.dart';
import 'src/ui/game_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  await SystemChrome.setPreferredOrientations(<DeviceOrientation>[
    DeviceOrientation.portraitUp,
    DeviceOrientation.portraitDown,
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);
  runApp(const TerritorialDomainApp());
}

final class TerritorialDomainApp extends StatefulWidget {
  const TerritorialDomainApp({super.key});

  @override
  State<TerritorialDomainApp> createState() => _TerritorialDomainAppState();
}

final class _TerritorialDomainAppState extends State<TerritorialDomainApp> {
  late final MultiTouchInputRouter _input;
  GameRuntime? _runtime;
  Object? _startupError;

  @override
  void initState() {
    super.initState();
    _input = MultiTouchInputRouter();
    try {
      _runtime = GameRuntime.open(_input);
    } on Object catch (error) {
      _startupError = error;
    }
  }

  @override
  void dispose() {
    _runtime?.dispose();
    _input.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ODPAR: Territorial Domain',
      debugShowCheckedModeBanner: false,
      theme: OdparDesign.theme,
      home: _runtime == null
          ? _StartupFailure(error: _startupError)
          : _BootGate(child: GameScreen(runtime: _runtime!, input: _input)),
    );
  }
}


final class _BootGate extends StatefulWidget {
  const _BootGate({required this.child});
  final Widget child;
  @override State<_BootGate> createState()=>_BootGateState();
}

final class _BootGateState extends State<_BootGate> with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  bool _done=false;
  @override void initState(){
    super.initState();
    _controller=AnimationController(vsync:this,duration:const Duration(milliseconds:1650))..forward();
    Future<void>.delayed(const Duration(milliseconds:1650),(){if(mounted)setState(()=>_done=true);});
  }
  @override void dispose(){_controller.dispose();super.dispose();}
  @override Widget build(BuildContext context){
    if(_done)return widget.child;
    return Scaffold(body:AnimatedBuilder(animation:_controller,builder:(BuildContext context,Widget? child){
      final double t=_controller.value;
      final double titleOpacity=((t-.16)/.34).clamp(0.0,1.0).toDouble();
      final double byOpacity=((t-.47)/.25).clamp(0.0,1.0).toDouble();
      return ColoredBox(color:OdparDesign.voidBlack,child:Center(child:Opacity(opacity:titleOpacity,child:Transform.translate(offset:Offset(0,(1-titleOpacity)*10),child:Column(mainAxisSize:MainAxisSize.min,children:<Widget>[
        const Text('ODPAR:',style:TextStyle(color:OdparDesign.accent,fontSize:13,fontWeight:FontWeight.w700,letterSpacing:3.2)),
        const SizedBox(height:8),const Text('TERRITORIAL DOMAIN',style:TextStyle(color:OdparDesign.text,fontSize:25,fontWeight:FontWeight.w600,letterSpacing:1.6)),
        const SizedBox(height:10),Opacity(opacity:byOpacity,child:const Text('BY KAOST032',style:TextStyle(color:OdparDesign.textMuted,fontSize:9,fontWeight:FontWeight.w600,letterSpacing:2.2))),
      ])))));
    }));
  }
}

final class _StartupFailure extends StatelessWidget {
  const _StartupFailure({required this.error});

  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(28),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 520),
              child: OdparPanel(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    Text('NATIVE CORE OFFLINE',
                        style: Theme.of(context).textTheme.titleLarge),
                    const SizedBox(height: 12),
                    const Text(
                      'La app no pudo validar el contrato nativo C11/FFI requerido. '
                      'Reinstala un paquete generado por el flujo oficial.',
                      style: TextStyle(color: OdparDesign.textMuted),
                    ),
                    const SizedBox(height: 16),
                    Text(
                      error?.toString() ?? 'Error nativo desconocido',
                      style: const TextStyle(
                        color: OdparDesign.danger,
                        fontFamily: 'monospace',
                        fontSize: 12,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
