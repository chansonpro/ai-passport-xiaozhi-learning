#!/usr/bin/env python3
"""Replay identical button events through the original HTML JS and firmware model."""
import json, pathlib, random, subprocess, sys, tempfile
root=pathlib.Path(__file__).resolve().parents[2]
rng=random.Random(513)
events=[[2,0]]+[[1,0]]*4+[[2,0]]*2+[[1,0],[2,0]]*8+[[2,1],[2,1],[1,0],[2,0]]+[[1,0]]*4+[[2,0]]
events += [[rng.randrange(3), 1 if rng.random()<.12 else 0] for _ in range(20000)]
js=r'''
const fs=require('fs'),vm=require('vm');
const html=fs.readFileSync(process.argv[2],'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const element={classList:{toggle(){}},textContent:'',innerHTML:''};
const context=vm.createContext({document:{querySelector:()=>({...element}),querySelectorAll:()=>[],addEventListener(){}},localStorage:{getItem:()=>null,setItem(){}},window:{},setTimeout,clearTimeout,console});
vm.runInContext(script,context);
vm.runInContext(`render=()=>{};let lastClip=-1;speak=(text)=>{phase='played';lastClip=words.findIndex(w=>w.en===text);if(lastClip<0){let q=words.findIndex(w=>'Find the '+w.en===text);lastClip=q>=0?4+q:8+lines.indexOf(text)}}`,context);
const events=JSON.parse(fs.readFileSync(process.argv[3]));
for(const [b,p] of events){
 vm.runInContext(`lastClip=-1;act('${['up','down','ok'][b]}',${p===1})`,context);
 console.log(vm.runInContext(`[page==='home'?1:page==='word'?2:page==='quiz'?3:mode===0?4:5,word,line,selected,round,choice,answered?1:0,mode,lastClip].join(' ')`,context));
}
'''
with tempfile.TemporaryDirectory() as td:
    p=pathlib.Path(td);(p/'compare.js').write_text(js);(p/'events.json').write_text(json.dumps(events))
    want=subprocess.check_output(['node',str(p/'compare.js'),str(root/'prototypes/learning-v1/index.html'),str(p/'events.json')],text=True).splitlines()
    got=subprocess.check_output([sys.argv[1],'trace'],input=''.join(f'{a} {b}\n' for a,b in events),text=True).splitlines()
    for i,(a,b) in enumerate(zip(want,got)):
        assert a==b, f'event {i} {events[max(0,i-6):i+1]}\nHTML: {a}\nFirmware: {b}'
    assert len(want)==len(got)==len(events)
    print(f'PASS: {len(events)} button events match original HTML state and requested audio exactly.')
