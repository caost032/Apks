import 'dart:async';

import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../platform/android_host.dart';
import '../design_system.dart';

final class MusicPanel extends StatefulWidget {
  const MusicPanel({required this.runtime, required this.reactivity, required this.onReactivityChanged, required this.pauseWithGame, required this.onPauseWithGameChanged, super.key});
  final GameRuntime runtime;
  final double reactivity;
  final ValueChanged<double> onReactivityChanged;
  final bool pauseWithGame;
  final ValueChanged<bool> onPauseWithGameChanged;
  @override State<MusicPanel> createState()=>_MusicPanelState();
}

final class _MusicPanelState extends State<MusicPanel>{
  final AndroidHost _host=AndroidHost.instance;
  Timer? _poll;
  Map<String,Object?> _state=const <String,Object?>{};
  @override void initState(){super.initState();_refresh();_poll=Timer.periodic(const Duration(milliseconds:500),(_)=>_refresh());}
  @override void dispose(){_poll?.cancel();super.dispose();}
  Future<void> _refresh()async{try{final s=await _host.musicState();if(mounted)setState(()=>_state=s);}catch(_){}}
  Future<void> _command(String cmd,{Object? value})async{try{final s=await _host.musicCommand(cmd,value:value);if(mounted)setState(()=>_state=s);}catch(_){}}
  Future<void> _add()async{try{final s=await _host.pickMusic();if(mounted)setState(()=>_state=s);}catch(_){}}
  Future<void> _addFeatured()async{await _command('featuredCatalog');}
  bool get playing=>_state['playing']==true;
  double number(String key,double fallback)=>(_state[key] as num?)?.toDouble()??fallback;
  @override Widget build(BuildContext context){
    final String title=_state['title']?.toString()??'';final String artist=_state['artist']?.toString()??'';
    final double duration=number('durationMs',0);final double position=number('positionMs',0).clamp(0,duration>0?duration:1).toDouble();
    final double volume=number('volume',1).clamp(0,1).toDouble();final bool shuffle=_state['shuffle']==true,repeat=_state['repeat']==true;
    final List<Object?> queue=(_state['queue'] as List<Object?>?)??const <Object?>[];
    final List<Object?> featured=(_state['featuredCatalog'] as List<Object?>?)??const <Object?>[];
    return ListView(padding:const EdgeInsets.all(16),children:<Widget>[
      Row(children:<Widget>[
        Container(width:54,height:54,decoration:BoxDecoration(color:const Color(0xFF15211F),border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(7)),child:const Icon(Icons.graphic_eq_rounded,color:OdparDesign.accent)),
        const SizedBox(width:12),Expanded(child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
          Text(title.isEmpty?'SIN CANCIÓN':title,maxLines:1,overflow:TextOverflow.ellipsis,style:const TextStyle(fontSize:12,fontWeight:FontWeight.w700)),
          const SizedBox(height:3),Text(artist.isEmpty?'Música local del dispositivo':artist,maxLines:1,overflow:TextOverflow.ellipsis,style:const TextStyle(fontSize:9,color:OdparDesign.textMuted)),
        ])),
        IconButton(onPressed:_add,tooltip:'Añadir música',icon:const Icon(Icons.library_music_outlined)),
      ]),
      Slider(value:duration>0?position:0,min:0,max:duration>0?duration:1,onChanged:duration>0?(double v)=>_command('seek',value:v.round()):null),
      Row(children:<Widget>[
        Text(_time(position),style:const TextStyle(fontSize:8,color:OdparDesign.textMuted)),const Spacer(),Text(_time(duration),style:const TextStyle(fontSize:8,color:OdparDesign.textMuted)),
      ]),
      Row(mainAxisAlignment:MainAxisAlignment.center,children:<Widget>[
        IconButton(onPressed:()=>_command('shuffle',value:!shuffle),icon:Icon(Icons.shuffle_rounded,color:shuffle?OdparDesign.accent:OdparDesign.textMuted)),
        IconButton(onPressed:()=>_command('previous'),icon:const Icon(Icons.skip_previous_rounded)),
        FilledButton.tonalIcon(onPressed:()=>_command(playing?'pause':'play'),icon:Icon(playing?Icons.pause_rounded:Icons.play_arrow_rounded),label:Text(playing?'PAUSA':'PLAY')),
        IconButton(onPressed:()=>_command('next'),icon:const Icon(Icons.skip_next_rounded)),
        IconButton(onPressed:()=>_command('repeat',value:!repeat),icon:Icon(Icons.repeat_rounded,color:repeat?OdparDesign.accent:OdparDesign.textMuted)),
      ]),
      const SizedBox(height:10),
      const Text('VOLUMEN',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,letterSpacing:1.1)),
      Slider(value:volume,onChanged:(double v)=>_command('volume',value:v)),
      const SizedBox(height:8),
      Row(children:<Widget>[
        const Expanded(child:Text('PAUSAR MÚSICA CON EL JUEGO',style:TextStyle(fontSize:9))),Switch(value:widget.pauseWithGame,onChanged:widget.onPauseWithGameChanged),
      ]),
      const Divider(color:OdparDesign.panelEdge),
      Row(children:<Widget>[
        const Expanded(child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
          Text('FEATURED MUSIC · AFTERIMAGE 0.2',style:TextStyle(fontSize:9,fontWeight:FontWeight.w800)),
          SizedBox(height:2),Text('7 Original Tracks · 5 Instrumental Reworks · Music catalog by kaost032',style:TextStyle(fontSize:8,color:OdparDesign.textMuted)),
        ])),
        TextButton.icon(onPressed:_addFeatured,icon:const Icon(Icons.add_to_queue_rounded,size:16),label:const Text('AÑADIR')),
      ]),
      const SizedBox(height:5),
      for(final Object? raw in featured) if(raw is Map) Padding(padding:const EdgeInsets.only(bottom:3),child:Row(children:<Widget>[
        Expanded(child:Text(raw['title']?.toString()??'',maxLines:1,overflow:TextOverflow.ellipsis,style:const TextStyle(fontSize:8.5))),
        Text(raw['kind']?.toString()=='Instrumental Rework'?'REWORK':'ORIGINAL',style:const TextStyle(fontSize:7,color:OdparDesign.textMuted,letterSpacing:.8)),
      ])),
      const SizedBox(height:7),
      const Text('Catálogo incluido con el juego. Propiedad/licencia del catálogo musical: kaost032.',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,height:1.4)),
      const Divider(color:OdparDesign.panelEdge),
      Row(children:<Widget>[
        const Expanded(child:Text('REACTIVIDAD MUSICAL',style:TextStyle(fontSize:9,fontWeight:FontWeight.w700))),Text('${(widget.reactivity*100).round()}%',style:const TextStyle(fontSize:9,color:OdparDesign.accent)),
      ]),
      Slider(value:widget.reactivity,min:0,max:1.5,divisions:30,onChanged:widget.onReactivityChanged),
      const Text('PCM decodificado en Android → JNI → analizador C. La música sólo modifica presentación; nunca territorio, IA, colisión, crafting, movimiento ni hash.',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,height:1.45)),
      const SizedBox(height:14),
      Text('COLA · ${queue.length}',style:const TextStyle(fontSize:8,color:OdparDesign.textMuted,letterSpacing:1.1)),
      for(final Object? raw in queue.take(8)) if(raw is Map) ListTile(dense:true,contentPadding:EdgeInsets.zero,title:Text(raw['title']?.toString()??'Audio',maxLines:1,overflow:TextOverflow.ellipsis,style:const TextStyle(fontSize:9)),subtitle:Text(raw['artist']?.toString()??'',style:const TextStyle(fontSize:8,color:OdparDesign.textMuted))),
    ]);
  }
  String _time(double ms){final int total=(ms/1000).round();return '${total~/60}:${(total%60).toString().padLeft(2,'0')}';}
}
