// PhizmOsc factory presets (7.0.0). Their tables are the built-in transwaves,
// named in each state (originA/B="3", builtinA/B), so no audio is stored here.
// Included by PluginProcessor.cpp.

struct PhizmOscFactoryPreset { const char* name; const char* xml; };
static const PhizmOscFactoryPreset kPhizmOscFactoryPresets[] = {
    { "Bell Pad", R"PHZ(<?xml version="1.0" encoding="UTF-8"?>

<PhizmOscState activeSlot="0" fileNameA="Brainiax.wav" originA="3" builtinA="Brainiax" cycleSizeA="2048" fileNameB=""
               cycleSizeB="2048">
  <Parameters>
    <PARAM id="attack" value="0.203000009059906"/>
    <PARAM id="attack2" value="0.2930000126361847"/>
    <PARAM id="bitCrush" value="11.28999996185303"/>
    <PARAM id="chorusDepth" value="0.1800000071525574"/>
    <PARAM id="chorusRate" value="5.820000171661377"/>
    <PARAM id="decay" value="4.835000038146973"/>
    <PARAM id="decay2" value="4.549000263214111"/>
    <PARAM id="detune" value="0.0"/>
    <PARAM id="evoLFODepth" value="0.07600000500679016"/>
    <PARAM id="evoLFORate" value="3.470000028610229"/>
    <PARAM id="evoPhaseCarry" value="0.0"/>
    <PARAM id="evoPointB_00" value="0.2870000004768372"/>
    <PARAM id="evoPointB_01" value="0.3520000278949738"/>
    <PARAM id="evoPointB_02" value="0.6460000276565552"/>
    <PARAM id="evoPointB_03" value="0.7800000309944153"/>
    <PARAM id="evoPointB_04" value="0.06200000271201134"/>
    <PARAM id="evoPointB_05" value="0.3930000066757202"/>
    <PARAM id="evoPointB_06" value="0.1220000088214874"/>
    <PARAM id="evoPointB_07" value="0.7080000042915344"/>
    <PARAM id="evoPointB_08" value="0.4680000245571136"/>
    <PARAM id="evoPointB_09" value="0.3080000281333923"/>
    <PARAM id="evoPointB_10" value="0.3730000257492065"/>
    <PARAM id="evoPointB_11" value="0.6740000247955322"/>
    <PARAM id="evoPointB_12" value="0.5700000524520874"/>
    <PARAM id="evoPointB_13" value="0.737000048160553"/>
    <PARAM id="evoPointB_14" value="0.9690000414848328"/>
    <PARAM id="evoPointB_15" value="0.1490000039339066"/>
    <PARAM id="evoPointB_16" value="0.5879999995231628"/>
    <PARAM id="evoPointB_17" value="0.8840000629425049"/>
    <PARAM id="evoPointB_18" value="0.1570000052452087"/>
    <PARAM id="evoPointB_19" value="0.2330000102519989"/>
    <PARAM id="evoPointB_20" value="0.08600000292062759"/>
    <PARAM id="evoPointB_21" value="0.6170000433921814"/>
    <PARAM id="evoPointB_22" value="0.2850000262260437"/>
    <PARAM id="evoPointB_23" value="0.4470000267028809"/>
    <PARAM id="evoPointB_24" value="0.359000027179718"/>
    <PARAM id="evoPointB_25" value="0.6030000448226929"/>
    <PARAM id="evoPointB_26" value="0.4180000126361847"/>
    <PARAM id="evoPointB_27" value="0.5240000486373901"/>
    <PARAM id="evoPointB_28" value="0.8260000348091125"/>
    <PARAM id="evoPointB_29" value="0.4710000157356262"/>
    <PARAM id="evoPointB_30" value="0.08600000292062759"/>
    <PARAM id="evoPointB_31" value="0.8110000491142273"/>
    <PARAM id="evoPoint_00" value="0.6850000619888306"/>
    <PARAM id="evoPoint_01" value="0.2120000123977661"/>
    <PARAM id="evoPoint_02" value="0.5870000123977661"/>
    <PARAM id="evoPoint_03" value="0.1260000020265579"/>
    <PARAM id="evoPoint_04" value="0.2440000176429749"/>
    <PARAM id="evoPoint_05" value="0.6840000152587891"/>
    <PARAM id="evoPoint_06" value="0.1340000033378601"/>
    <PARAM id="evoPoint_07" value="0.8680000305175781"/>
    <PARAM id="evoPoint_08" value="0.3680000305175781"/>
    <PARAM id="evoPoint_09" value="0.2939999997615814"/>
    <PARAM id="evoPoint_10" value="0.7900000214576721"/>
    <PARAM id="evoPoint_11" value="0.4030000269412994"/>
    <PARAM id="evoPoint_12" value="0.3960000276565552"/>
    <PARAM id="evoPoint_13" value="0.6680000424385071"/>
    <PARAM id="evoPoint_14" value="0.03500000014901161"/>
    <PARAM id="evoPoint_15" value="0.5940000414848328"/>
    <PARAM id="evoPoint_16" value="0.7400000095367432"/>
    <PARAM id="evoPoint_17" value="0.04400000348687172"/>
    <PARAM id="evoPoint_18" value="0.7420000433921814"/>
    <PARAM id="evoPoint_19" value="0.02200000174343586"/>
    <PARAM id="evoPoint_20" value="0.08300000429153442"/>
    <PARAM id="evoPoint_21" value="0.4820000231266022"/>
    <PARAM id="evoPoint_22" value="0.1900000125169754"/>
    <PARAM id="evoPoint_23" value="0.7050000429153442"/>
    <PARAM id="evoPoint_24" value="0.417000025510788"/>
    <PARAM id="evoPoint_25" value="0.2430000156164169"/>
    <PARAM id="evoPoint_26" value="0.8890000581741333"/>
    <PARAM id="evoPoint_27" value="0.9430000185966492"/>
    <PARAM id="evoPoint_28" value="0.7190000414848328"/>
    <PARAM id="evoPoint_29" value="0.3210000097751617"/>
    <PARAM id="evoPoint_30" value="0.1630000025033951"/>
    <PARAM id="evoPoint_31" value="0.9000000357627869"/>
    <PARAM id="evoStepped" value="0.0"/>
    <PARAM id="evoSteppedB" value="0.0"/>
    <PARAM id="evoTime" value="5.922000408172607"/>
    <PARAM id="filterAtt" value="1.813000082969666"/>
    <PARAM id="filterDec" value="2.807000160217285"/>
    <PARAM id="filterEnvAmt" value="-1.0"/>
    <PARAM id="filterFreq" value="3387.699951171875"/>
    <PARAM id="filterLFODep" value="0.9780000448226929"/>
    <PARAM id="filterPerVoice" value="0.0"/>
    <PARAM id="filterQ" value="1.009999990463257"/>
    <PARAM id="filterRel" value="0.1620000153779984"/>
    <PARAM id="filterSus" value="0.2670000195503235"/>
    <PARAM id="frameInterp" value="1.0"/>
    <PARAM id="gain" value="0.909000039100647"/>
    <PARAM id="glide" value="2.26800012588501"/>
    <PARAM id="grit" value="0.04500000178813934"/>
    <PARAM id="jumpProb" value="0.5390000343322754"/>
    <PARAM id="mono" value="0.0"/>
    <PARAM id="noiseLevel" value="0.0"/>
    <PARAM id="octaveA" value="-1.0"/>
    <PARAM id="octaveB" value="0.0"/>
    <PARAM id="oscMix" value="0.4460000097751617"/>
    <PARAM id="pitchEnvAmt" value="8.5"/>
    <PARAM id="pitchEnvAtt" value="0.3470000028610229"/>
    <PARAM id="pitchEnvDec" value="0.2730000019073486"/>
    <PARAM id="pitchLFO" value="0.0"/>
    <PARAM id="pitchLFORate" value="0.9999999403953552"/>
    <PARAM id="posLFODepth" value="0.07500000298023224"/>
    <PARAM id="posLFORate" value="0.4199999868869781"/>
    <PARAM id="release" value="7.544000148773193"/>
    <PARAM id="release2" value="1.857000112533569"/>
    <PARAM id="reverbDamp" value="0.9620000720024109"/>
    <PARAM id="reverbSize" value="0.242000013589859"/>
    <PARAM id="reverbWet" value="0.4860000312328339"/>
    <PARAM id="ringMod" value="0.1510000079870224"/>
    <PARAM id="scanStyle" value="1.0"/>
    <PARAM id="spread" value="34.60000228881836"/>
    <PARAM id="stereoPhase" value="0.232000008225441"/>
    <PARAM id="stereoWidth" value="0.03500000014901161"/>
    <PARAM id="sustain" value="0.7980000376701355"/>
    <PARAM id="sustain2" value="0.3680000305175781"/>
    <PARAM id="uniDetune" value="0.1000000014901161"/>
    <PARAM id="velToFrame" value="1.0"/>
    <PARAM id="cycleSizeA"/>
    <PARAM id="cycleSizeB"/>
    <PARAM id="evoBPhaseOff"/>
    <PARAM id="evoRestart"/>
    <PARAM id="evoTimeB"/>
    <PARAM id="frameSnap"/>
    <PARAM id="guiWidth" value="1190.0"/>
    <PARAM id="keytrack"/>
    <PARAM id="twAmt"/>
    <PARAM id="twAtt"/>
    <PARAM id="twDec"/>
    <PARAM id="twRel"/>
    <PARAM id="twSus"/>
    <PARAM id="twToFilter"/>
    <PARAM id="twVelAmt"/>
  </Parameters>
</PhizmOscState>)PHZ" },
    { "Deep Digital Bubbles", R"PHZ(<?xml version="1.0" encoding="UTF-8"?>

<PhizmOscState activeSlot="1" fileNameA="Long Gongwave.wav" originA="3" builtinA="Long Gongwave" cycleSizeA="2048" fileNameB="Get Even X.wav" originB="3" builtinB="Get Even X"
               cycleSizeB="2048">
  <Parameters>
    <PARAM id="attack" value="2.377000093460083"/>
    <PARAM id="attack2" value="0.01000000070780516"/>
    <PARAM id="bitCrush" value="4.0"/>
    <PARAM id="chorusDepth" value="0.5890000462532043"/>
    <PARAM id="chorusRate" value="0.2300000041723251"/>
    <PARAM id="decay" value="0.300000011920929"/>
    <PARAM id="decay2" value="0.300000011920929"/>
    <PARAM id="detune" value="0.0"/>
    <PARAM id="evoLFODepth" value="0.0"/>
    <PARAM id="evoLFORate" value="0.2999999821186066"/>
    <PARAM id="evoPhaseCarry" value="1.0"/>
    <PARAM id="evoPointB_00" value="0.0"/>
    <PARAM id="evoPointB_01" value="0.03200000151991844"/>
    <PARAM id="evoPointB_02" value="0.06500000506639481"/>
    <PARAM id="evoPointB_03" value="0.386000007390976"/>
    <PARAM id="evoPointB_04" value="0.484000027179718"/>
    <PARAM id="evoPointB_05" value="0.609000027179718"/>
    <PARAM id="evoPointB_06" value="0.7120000123977661"/>
    <PARAM id="evoPointB_07" value="0.7880000472068787"/>
    <PARAM id="evoPointB_08" value="0.8370000123977661"/>
    <PARAM id="evoPointB_09" value="0.9180000424385071"/>
    <PARAM id="evoPointB_10" value="0.0"/>
    <PARAM id="evoPointB_11" value="0.0"/>
    <PARAM id="evoPointB_12" value="0.0"/>
    <PARAM id="evoPointB_13" value="0.6470000147819519"/>
    <PARAM id="evoPointB_14" value="0.5870000123977661"/>
    <PARAM id="evoPointB_15" value="0.4780000150203705"/>
    <PARAM id="evoPointB_16" value="0.4570000171661377"/>
    <PARAM id="evoPointB_17" value="0.0"/>
    <PARAM id="evoPointB_18" value="0.2450000047683716"/>
    <PARAM id="evoPointB_19" value="0.5490000247955322"/>
    <PARAM id="evoPointB_20" value="0.6520000100135803"/>
    <PARAM id="evoPointB_21" value="0.03800000250339508"/>
    <PARAM id="evoPointB_22" value="0.1680000126361847"/>
    <PARAM id="evoPointB_23" value="0.5380000472068787"/>
    <PARAM id="evoPointB_24" value="0.7170000076293945"/>
    <PARAM id="evoPointB_25" value="0.609000027179718"/>
    <PARAM id="evoPointB_26" value="0.3150000274181366"/>
    <PARAM id="evoPointB_27" value="0.261000007390976"/>
    <PARAM id="evoPointB_28" value="0.2770000100135803"/>
    <PARAM id="evoPointB_29" value="0.3800000250339508"/>
    <PARAM id="evoPointB_30" value="0.5049999952316284"/>
    <PARAM id="evoPointB_31" value="0.4130000174045563"/>
    <PARAM id="evoPoint_00" value="0.125"/>
    <PARAM id="evoPoint_01" value="0.2120000123977661"/>
    <PARAM id="evoPoint_02" value="0.06500000506639481"/>
    <PARAM id="evoPoint_03" value="0.3700000047683716"/>
    <PARAM id="evoPoint_04" value="0.495000034570694"/>
    <PARAM id="evoPoint_05" value="0.6740000247955322"/>
    <PARAM id="evoPoint_06" value="0.7550000548362732"/>
    <PARAM id="evoPoint_07" value="0.4290000200271606"/>
    <PARAM id="evoPoint_08" value="0.0"/>
    <PARAM id="evoPoint_09" value="0.09800000488758087"/>
    <PARAM id="evoPoint_10" value="0.5710000395774841"/>
    <PARAM id="evoPoint_11" value="0.7170000076293945"/>
    <PARAM id="evoPoint_12" value="0.7720000147819519"/>
    <PARAM id="evoPoint_13" value="0.6630000472068787"/>
    <PARAM id="evoPoint_14" value="0.5220000147819519"/>
    <PARAM id="evoPoint_15" value="0.5649999976158142"/>
    <PARAM id="evoPoint_16" value="0.609000027179718"/>
    <PARAM id="evoPoint_17" value="0.6960000395774841"/>
    <PARAM id="evoPoint_18" value="0.7610000371932983"/>
    <PARAM id="evoPoint_19" value="0.7170000076293945"/>
    <PARAM id="evoPoint_20" value="0.4020000100135803"/>
    <PARAM id="evoPoint_21" value="0.3750000298023224"/>
    <PARAM id="evoPoint_22" value="0.3370000123977661"/>
    <PARAM id="evoPoint_23" value="0.3320000171661377"/>
    <PARAM id="evoPoint_24" value="0.386000007390976"/>
    <PARAM id="evoPoint_25" value="0.4780000150203705"/>
    <PARAM id="evoPoint_26" value="0.04900000244379044"/>
    <PARAM id="evoPoint_27" value="0.1030000075697899"/>
    <PARAM id="evoPoint_28" value="0.1960000097751617"/>
    <PARAM id="evoPoint_29" value="0.2390000075101852"/>
    <PARAM id="evoPoint_30" value="0.02200000174343586"/>
    <PARAM id="evoPoint_31" value="0.0"/>
    <PARAM id="evoStepped" value="0.0"/>
    <PARAM id="evoSteppedB" value="0.0"/>
    <PARAM id="evoTime" value="33.09700012207031"/>
    <PARAM id="filterAtt" value="1.931000113487244"/>
    <PARAM id="filterDec" value="1.690000176429749"/>
    <PARAM id="filterEnvAmt" value="0.6980000734329224"/>
    <PARAM id="filterFreq" value="2569.5"/>
    <PARAM id="filterLFODep" value="0.0"/>
    <PARAM id="filterPerVoice" value="1.0"/>
    <PARAM id="filterQ" value="1.909999966621399"/>
    <PARAM id="filterRel" value="3.689000129699707"/>
    <PARAM id="filterSus" value="0.550000011920929"/>
    <PARAM id="frameInterp" value="1.0"/>
    <PARAM id="gain" value="0.7000000476837158"/>
    <PARAM id="glide" value="0.0"/>
    <PARAM id="grit" value="0.0"/>
    <PARAM id="jumpProb" value="0.0"/>
    <PARAM id="mono" value="0.0"/>
    <PARAM id="noiseLevel" value="0.0"/>
    <PARAM id="octaveA" value="-1.0"/>
    <PARAM id="octaveB" value="-2.0"/>
    <PARAM id="oscMix" value="0.6410000324249268"/>
    <PARAM id="pitchEnvAmt" value="0.0"/>
    <PARAM id="pitchEnvAtt" value="0.05000000074505806"/>
    <PARAM id="pitchEnvDec" value="0.300000011920929"/>
    <PARAM id="pitchLFO" value="0.0"/>
    <PARAM id="pitchLFORate" value="0.9999999403953552"/>
    <PARAM id="posLFODepth" value="0.0"/>
    <PARAM id="posLFORate" value="0.4999999701976776"/>
    <PARAM id="release" value="3.389000177383423"/>
    <PARAM id="release2" value="0.4999999403953552"/>
    <PARAM id="reverbDamp" value="0.5"/>
    <PARAM id="reverbSize" value="0.5"/>
    <PARAM id="reverbWet" value="0.0"/>
    <PARAM id="ringMod" value="0.0"/>
    <PARAM id="scanStyle" value="0.0"/>
    <PARAM id="spread" value="14.60000038146973"/>
    <PARAM id="stereoPhase" value="0.4540000259876251"/>
    <PARAM id="stereoWidth" value="0.734000027179718"/>
    <PARAM id="sustain" value="0.800000011920929"/>
    <PARAM id="sustain2" value="0.800000011920929"/>
    <PARAM id="uniDetune" value="17.39999961853027"/>
    <PARAM id="velToFrame" value="1.0"/>
    <PARAM id="cycleSizeA"/>
    <PARAM id="cycleSizeB"/>
    <PARAM id="evoBPhaseOff"/>
    <PARAM id="evoRestart"/>
    <PARAM id="evoTimeB"/>
    <PARAM id="frameSnap"/>
    <PARAM id="guiWidth"/>
    <PARAM id="keytrack"/>
    <PARAM id="twAmt"/>
    <PARAM id="twAtt"/>
    <PARAM id="twDec"/>
    <PARAM id="twRel"/>
    <PARAM id="twSus"/>
    <PARAM id="twToFilter"/>
    <PARAM id="twVelAmt"/>
  </Parameters>
</PhizmOscState>)PHZ" },
    { "Deep Dital Rumbles", R"PHZ(<?xml version="1.0" encoding="UTF-8"?>

<PhizmOscState activeSlot="1" fileNameA="Long Gongwave.wav" originA="3" builtinA="Long Gongwave" cycleSizeA="2048" fileNameB="Get Even X.wav" originB="3" builtinB="Get Even X"
               cycleSizeB="2048">
  <Parameters>
    <PARAM id="attack" value="2.377000093460083"/>
    <PARAM id="attack2" value="0.01000000070780516"/>
    <PARAM id="bitCrush" value="4.0"/>
    <PARAM id="chorusDepth" value="0.3520000278949738"/>
    <PARAM id="chorusRate" value="0.1700000017881393"/>
    <PARAM id="decay" value="0.300000011920929"/>
    <PARAM id="decay2" value="0.300000011920929"/>
    <PARAM id="detune" value="0.0"/>
    <PARAM id="evoLFODepth" value="0.0"/>
    <PARAM id="evoLFORate" value="0.2999999821186066"/>
    <PARAM id="evoPhaseCarry" value="1.0"/>
    <PARAM id="evoPointB_00" value="0.0"/>
    <PARAM id="evoPointB_01" value="0.03200000151991844"/>
    <PARAM id="evoPointB_02" value="0.06500000506639481"/>
    <PARAM id="evoPointB_03" value="0.386000007390976"/>
    <PARAM id="evoPointB_04" value="0.484000027179718"/>
    <PARAM id="evoPointB_05" value="0.609000027179718"/>
    <PARAM id="evoPointB_06" value="0.7120000123977661"/>
    <PARAM id="evoPointB_07" value="0.7880000472068787"/>
    <PARAM id="evoPointB_08" value="0.8370000123977661"/>
    <PARAM id="evoPointB_09" value="0.9180000424385071"/>
    <PARAM id="evoPointB_10" value="0.0"/>
    <PARAM id="evoPointB_11" value="0.0"/>
    <PARAM id="evoPointB_12" value="0.0"/>
    <PARAM id="evoPointB_13" value="0.6470000147819519"/>
    <PARAM id="evoPointB_14" value="0.5870000123977661"/>
    <PARAM id="evoPointB_15" value="0.4780000150203705"/>
    <PARAM id="evoPointB_16" value="0.4570000171661377"/>
    <PARAM id="evoPointB_17" value="0.0"/>
    <PARAM id="evoPointB_18" value="0.2450000047683716"/>
    <PARAM id="evoPointB_19" value="0.5490000247955322"/>
    <PARAM id="evoPointB_20" value="0.6520000100135803"/>
    <PARAM id="evoPointB_21" value="0.03800000250339508"/>
    <PARAM id="evoPointB_22" value="0.1680000126361847"/>
    <PARAM id="evoPointB_23" value="0.5380000472068787"/>
    <PARAM id="evoPointB_24" value="0.7170000076293945"/>
    <PARAM id="evoPointB_25" value="0.609000027179718"/>
    <PARAM id="evoPointB_26" value="0.3150000274181366"/>
    <PARAM id="evoPointB_27" value="0.261000007390976"/>
    <PARAM id="evoPointB_28" value="0.2770000100135803"/>
    <PARAM id="evoPointB_29" value="0.3800000250339508"/>
    <PARAM id="evoPointB_30" value="0.5049999952316284"/>
    <PARAM id="evoPointB_31" value="0.4130000174045563"/>
    <PARAM id="evoPoint_00" value="0.125"/>
    <PARAM id="evoPoint_01" value="0.2120000123977661"/>
    <PARAM id="evoPoint_02" value="0.06500000506639481"/>
    <PARAM id="evoPoint_03" value="0.3700000047683716"/>
    <PARAM id="evoPoint_04" value="0.495000034570694"/>
    <PARAM id="evoPoint_05" value="0.6740000247955322"/>
    <PARAM id="evoPoint_06" value="0.7550000548362732"/>
    <PARAM id="evoPoint_07" value="0.4290000200271606"/>
    <PARAM id="evoPoint_08" value="0.0"/>
    <PARAM id="evoPoint_09" value="0.09800000488758087"/>
    <PARAM id="evoPoint_10" value="0.5710000395774841"/>
    <PARAM id="evoPoint_11" value="0.7170000076293945"/>
    <PARAM id="evoPoint_12" value="0.7720000147819519"/>
    <PARAM id="evoPoint_13" value="0.6630000472068787"/>
    <PARAM id="evoPoint_14" value="0.5220000147819519"/>
    <PARAM id="evoPoint_15" value="0.5649999976158142"/>
    <PARAM id="evoPoint_16" value="0.609000027179718"/>
    <PARAM id="evoPoint_17" value="0.6960000395774841"/>
    <PARAM id="evoPoint_18" value="0.7610000371932983"/>
    <PARAM id="evoPoint_19" value="0.7170000076293945"/>
    <PARAM id="evoPoint_20" value="0.4020000100135803"/>
    <PARAM id="evoPoint_21" value="0.3750000298023224"/>
    <PARAM id="evoPoint_22" value="0.3370000123977661"/>
    <PARAM id="evoPoint_23" value="0.3320000171661377"/>
    <PARAM id="evoPoint_24" value="0.386000007390976"/>
    <PARAM id="evoPoint_25" value="0.4780000150203705"/>
    <PARAM id="evoPoint_26" value="0.04900000244379044"/>
    <PARAM id="evoPoint_27" value="0.1030000075697899"/>
    <PARAM id="evoPoint_28" value="0.1960000097751617"/>
    <PARAM id="evoPoint_29" value="0.2390000075101852"/>
    <PARAM id="evoPoint_30" value="0.02200000174343586"/>
    <PARAM id="evoPoint_31" value="0.0"/>
    <PARAM id="evoStepped" value="0.0"/>
    <PARAM id="evoSteppedB" value="0.0"/>
    <PARAM id="evoTime" value="19.67200088500977"/>
    <PARAM id="filterAtt" value="1.931000113487244"/>
    <PARAM id="filterDec" value="1.690000176429749"/>
    <PARAM id="filterEnvAmt" value="0.6980000734329224"/>
    <PARAM id="filterFreq" value="949.7000122070312"/>
    <PARAM id="filterLFODep" value="0.0"/>
    <PARAM id="filterPerVoice" value="1.0"/>
    <PARAM id="filterQ" value="1.600000023841858"/>
    <PARAM id="filterRel" value="3.689000129699707"/>
    <PARAM id="filterSus" value="0.550000011920929"/>
    <PARAM id="frameInterp" value="1.0"/>
    <PARAM id="gain" value="0.7000000476837158"/>
    <PARAM id="glide" value="0.0"/>
    <PARAM id="grit" value="0.4100000262260437"/>
    <PARAM id="jumpProb" value="0.0"/>
    <PARAM id="mono" value="0.0"/>
    <PARAM id="noiseLevel" value="0.0"/>
    <PARAM id="octaveA" value="-1.0"/>
    <PARAM id="octaveB" value="-2.0"/>
    <PARAM id="oscMix" value="0.6150000095367432"/>
    <PARAM id="pitchEnvAmt" value="0.0"/>
    <PARAM id="pitchEnvAtt" value="0.05000000074505806"/>
    <PARAM id="pitchEnvDec" value="0.300000011920929"/>
    <PARAM id="pitchLFO" value="0.0"/>
    <PARAM id="pitchLFORate" value="0.9999999403953552"/>
    <PARAM id="posLFODepth" value="0.0"/>
    <PARAM id="posLFORate" value="0.4999999701976776"/>
    <PARAM id="release" value="3.389000177383423"/>
    <PARAM id="release2" value="0.4999999403953552"/>
    <PARAM id="reverbDamp" value="0.5"/>
    <PARAM id="reverbSize" value="0.5"/>
    <PARAM id="reverbWet" value="0.2040000110864639"/>
    <PARAM id="ringMod" value="0.0"/>
    <PARAM id="scanStyle" value="0.0"/>
    <PARAM id="spread" value="14.60000038146973"/>
    <PARAM id="stereoPhase" value="0.4540000259876251"/>
    <PARAM id="stereoWidth" value="0.734000027179718"/>
    <PARAM id="sustain" value="0.800000011920929"/>
    <PARAM id="sustain2" value="0.800000011920929"/>
    <PARAM id="uniDetune" value="17.39999961853027"/>
    <PARAM id="velToFrame" value="1.0"/>
    <PARAM id="cycleSizeA"/>
    <PARAM id="cycleSizeB"/>
    <PARAM id="evoBPhaseOff"/>
    <PARAM id="evoRestart"/>
    <PARAM id="evoTimeB"/>
    <PARAM id="frameSnap"/>
    <PARAM id="guiWidth"/>
    <PARAM id="keytrack"/>
    <PARAM id="twAmt"/>
    <PARAM id="twAtt"/>
    <PARAM id="twDec"/>
    <PARAM id="twRel"/>
    <PARAM id="twSus"/>
    <PARAM id="twToFilter"/>
    <PARAM id="twVelAmt"/>
  </Parameters>
</PhizmOscState>)PHZ" },
    { "Poly Swarm", R"PHZ(<?xml version="1.0" encoding="UTF-8"?>

<PhizmOscState activeSlot="1" fileNameA="Polymod Wave.wav" originA="3" builtinA="Polymod Wave" cycleSizeA="2048"
               fileNameB="Organwave X.wav" originB="3" builtinB="Organwave X" cycleSizeB="2048">
  <Parameters>
    <PARAM id="attack" value="0.01000000070780516"/>
    <PARAM id="attack2" value="0.01000000070780516"/>
    <PARAM id="bitCrush" value="16.0"/>
    <PARAM id="chorusDepth" value="0.6680000424385071"/>
    <PARAM id="chorusRate" value="0.3799999952316284"/>
    <PARAM id="decay" value="0.300000011920929"/>
    <PARAM id="decay2" value="0.300000011920929"/>
    <PARAM id="detune" value="0.0"/>
    <PARAM id="evoLFODepth" value="0.0"/>
    <PARAM id="evoLFORate" value="0.2999999821186066"/>
    <PARAM id="evoPhaseCarry" value="1.0"/>
    <PARAM id="evoPointB_00" value="0.0"/>
    <PARAM id="evoPointB_01" value="0.1300000101327896"/>
    <PARAM id="evoPointB_02" value="0.1090000048279762"/>
    <PARAM id="evoPointB_03" value="0.1030000075697899"/>
    <PARAM id="evoPointB_04" value="0.1030000075697899"/>
    <PARAM id="evoPointB_05" value="0.1030000075697899"/>
    <PARAM id="evoPointB_06" value="0.1030000075697899"/>
    <PARAM id="evoPointB_07" value="0.08700000494718552"/>
    <PARAM id="evoPointB_08" value="0.07600000500679016"/>
    <PARAM id="evoPointB_09" value="0.07600000500679016"/>
    <PARAM id="evoPointB_10" value="0.08700000494718552"/>
    <PARAM id="evoPointB_11" value="0.1200000047683716"/>
    <PARAM id="evoPointB_12" value="0.1850000023841858"/>
    <PARAM id="evoPointB_13" value="0.3320000171661377"/>
    <PARAM id="evoPointB_14" value="0.5649999976158142"/>
    <PARAM id="evoPointB_15" value="0.7390000224113464"/>
    <PARAM id="evoPointB_16" value="0.8910000324249268"/>
    <PARAM id="evoPointB_17" value="0.9780000448226929"/>
    <PARAM id="evoPointB_18" value="1.0"/>
    <PARAM id="evoPointB_19" value="0.9950000643730164"/>
    <PARAM id="evoPointB_20" value="0.8040000200271606"/>
    <PARAM id="evoPointB_21" value="0.5110000371932983"/>
    <PARAM id="evoPointB_22" value="0.3480000197887421"/>
    <PARAM id="evoPointB_23" value="0.3260000050067902"/>
    <PARAM id="evoPointB_24" value="0.3100000023841858"/>
    <PARAM id="evoPointB_25" value="0.3260000050067902"/>
    <PARAM id="evoPointB_26" value="0.3370000123977661"/>
    <PARAM id="evoPointB_27" value="0.3800000250339508"/>
    <PARAM id="evoPointB_28" value="0.5160000324249268"/>
    <PARAM id="evoPointB_29" value="0.5540000200271606"/>
    <PARAM id="evoPointB_30" value="0.4350000321865082"/>
    <PARAM id="evoPointB_31" value="0.4290000200271606"/>
    <PARAM id="evoPoint_00" value="0.0"/>
    <PARAM id="evoPoint_01" value="0.06500000506639481"/>
    <PARAM id="evoPoint_02" value="0.09200000762939453"/>
    <PARAM id="evoPoint_03" value="0.1200000047683716"/>
    <PARAM id="evoPoint_04" value="0.1520000100135803"/>
    <PARAM id="evoPoint_05" value="0.1790000051259995"/>
    <PARAM id="evoPoint_06" value="0.1960000097751617"/>
    <PARAM id="evoPoint_07" value="0.2990000247955322"/>
    <PARAM id="evoPoint_08" value="0.3040000200271606"/>
    <PARAM id="evoPoint_09" value="0.4290000200271606"/>
    <PARAM id="evoPoint_10" value="0.6520000100135803"/>
    <PARAM id="evoPoint_11" value="0.7450000643730164"/>
    <PARAM id="evoPoint_12" value="0.815000057220459"/>
    <PARAM id="evoPoint_13" value="0.4130000174045563"/>
    <PARAM id="evoPoint_14" value="0.2830000221729279"/>
    <PARAM id="evoPoint_15" value="0.1790000051259995"/>
    <PARAM id="evoPoint_16" value="0.1580000072717667"/>
    <PARAM id="evoPoint_17" value="0.1580000072717667"/>
    <PARAM id="evoPoint_18" value="0.1900000125169754"/>
    <PARAM id="evoPoint_19" value="0.2660000026226044"/>
    <PARAM id="evoPoint_20" value="0.4570000171661377"/>
    <PARAM id="evoPoint_21" value="0.5760000348091125"/>
    <PARAM id="evoPoint_22" value="0.6630000472068787"/>
    <PARAM id="evoPoint_23" value="0.7120000123977661"/>
    <PARAM id="evoPoint_24" value="0.7390000224113464"/>
    <PARAM id="evoPoint_25" value="0.7450000643730164"/>
    <PARAM id="evoPoint_26" value="0.7280000448226929"/>
    <PARAM id="evoPoint_27" value="0.690000057220459"/>
    <PARAM id="evoPoint_28" value="0.6410000324249268"/>
    <PARAM id="evoPoint_29" value="0.5870000123977661"/>
    <PARAM id="evoPoint_30" value="0.5049999952316284"/>
    <PARAM id="evoPoint_31" value="0.5600000023841858"/>
    <PARAM id="evoStepped" value="0.0"/>
    <PARAM id="evoSteppedB" value="0.0"/>
    <PARAM id="evoTime" value="4.0"/>
    <PARAM id="filterAtt" value="0.01000000070780516"/>
    <PARAM id="filterDec" value="0.4999999403953552"/>
    <PARAM id="filterEnvAmt" value="0.0"/>
    <PARAM id="filterFreq" value="4960.80029296875"/>
    <PARAM id="filterLFODep" value="0.3580000102519989"/>
    <PARAM id="filterPerVoice" value="0.0"/>
    <PARAM id="filterQ" value="3.289999723434448"/>
    <PARAM id="filterRel" value="0.4999999403953552"/>
    <PARAM id="filterSus" value="0.0"/>
    <PARAM id="frameInterp" value="1.0"/>
    <PARAM id="gain" value="0.7000000476837158"/>
    <PARAM id="glide" value="0.02800000086426735"/>
    <PARAM id="grit" value="0.0"/>
    <PARAM id="jumpProb" value="0.0"/>
    <PARAM id="mono" value="0.0"/>
    <PARAM id="noiseLevel" value="0.0"/>
    <PARAM id="octaveA" value="-1.0"/>
    <PARAM id="octaveB" value="0.0"/>
    <PARAM id="oscMix" value="0.5680000185966492"/>
    <PARAM id="pitchEnvAmt" value="0.0"/>
    <PARAM id="pitchEnvAtt" value="0.05000000074505806"/>
    <PARAM id="pitchEnvDec" value="0.300000011920929"/>
    <PARAM id="pitchLFO" value="0.0"/>
    <PARAM id="pitchLFORate" value="0.9999999403953552"/>
    <PARAM id="posLFODepth" value="0.0"/>
    <PARAM id="posLFORate" value="0.4999999701976776"/>
    <PARAM id="release" value="0.4999999403953552"/>
    <PARAM id="release2" value="0.4999999403953552"/>
    <PARAM id="reverbDamp" value="0.5"/>
    <PARAM id="reverbSize" value="0.5"/>
    <PARAM id="reverbWet" value="0.0"/>
    <PARAM id="ringMod" value="0.0"/>
    <PARAM id="scanStyle" value="0.0"/>
    <PARAM id="spread" value="13.0"/>
    <PARAM id="stereoPhase" value="0.1920000016689301"/>
    <PARAM id="stereoWidth" value="0.5"/>
    <PARAM id="sustain" value="0.800000011920929"/>
    <PARAM id="sustain2" value="0.800000011920929"/>
    <PARAM id="uniDetune" value="18.20000076293945"/>
    <PARAM id="velToFrame" value="0.0"/>
    <PARAM id="cycleSizeA"/>
    <PARAM id="cycleSizeB"/>
    <PARAM id="evoBPhaseOff"/>
    <PARAM id="evoRestart"/>
    <PARAM id="evoTimeB"/>
    <PARAM id="frameSnap"/>
    <PARAM id="guiWidth"/>
    <PARAM id="keytrack"/>
    <PARAM id="twAmt"/>
    <PARAM id="twAtt"/>
    <PARAM id="twDec"/>
    <PARAM id="twRel"/>
    <PARAM id="twSus"/>
    <PARAM id="twToFilter"/>
    <PARAM id="twVelAmt"/>
  </Parameters>
</PhizmOscState>)PHZ" },
    { "Rez Squelch", R"PHZ(<?xml version="1.0" encoding="UTF-8"?>

<PhizmOscState activeSlot="0" fileNameA="Polymod Wave.wav" originA="3" builtinA="Polymod Wave" cycleSizeA="2048"
               fileNameB="Rez Loopwave.wav" originB="3" builtinB="Rez Loopwave" cycleSizeB="2048">
  <Parameters>
    <PARAM id="attack" value="0.01000000070780516"/>
    <PARAM id="attack2" value="0.001000000047497451"/>
    <PARAM id="bitCrush" value="16.0"/>
    <PARAM id="chorusDepth" value="0.0"/>
    <PARAM id="chorusRate" value="0.5"/>
    <PARAM id="decay" value="0.300000011920929"/>
    <PARAM id="decay2" value="0.4600000083446503"/>
    <PARAM id="detune" value="0.0"/>
    <PARAM id="evoLFODepth" value="0.0"/>
    <PARAM id="evoLFORate" value="0.2999999821186066"/>
    <PARAM id="evoPhaseCarry" value="0.0"/>
    <PARAM id="evoPointB_00" value="0.03800000250339508"/>
    <PARAM id="evoPointB_01" value="0.08700000494718552"/>
    <PARAM id="evoPointB_02" value="0.174000009894371"/>
    <PARAM id="evoPointB_03" value="0.2720000147819519"/>
    <PARAM id="evoPointB_04" value="0.4350000321865082"/>
    <PARAM id="evoPointB_05" value="0.6740000247955322"/>
    <PARAM id="evoPointB_06" value="0.9130000472068787"/>
    <PARAM id="evoPointB_07" value="1.0"/>
    <PARAM id="evoPointB_08" value="1.0"/>
    <PARAM id="evoPointB_09" value="1.0"/>
    <PARAM id="evoPointB_10" value="1.0"/>
    <PARAM id="evoPointB_11" value="1.0"/>
    <PARAM id="evoPointB_12" value="1.0"/>
    <PARAM id="evoPointB_13" value="1.0"/>
    <PARAM id="evoPointB_14" value="1.0"/>
    <PARAM id="evoPointB_15" value="0.9570000171661377"/>
    <PARAM id="evoPointB_16" value="0.7450000643730164"/>
    <PARAM id="evoPointB_17" value="0.5870000123977661"/>
    <PARAM id="evoPointB_18" value="0.4350000321865082"/>
    <PARAM id="evoPointB_19" value="0.3370000123977661"/>
    <PARAM id="evoPointB_20" value="0.2660000026226044"/>
    <PARAM id="evoPointB_21" value="0.2170000076293945"/>
    <PARAM id="evoPointB_22" value="0.1680000126361847"/>
    <PARAM id="evoPointB_23" value="0.136000007390976"/>
    <PARAM id="evoPointB_24" value="0.1090000048279762"/>
    <PARAM id="evoPointB_25" value="0.08700000494718552"/>
    <PARAM id="evoPointB_26" value="0.07100000232458115"/>
    <PARAM id="evoPointB_27" value="0.05400000140070915"/>
    <PARAM id="evoPointB_28" value="0.03800000250339508"/>
    <PARAM id="evoPointB_29" value="0.02700000070035458"/>
    <PARAM id="evoPointB_30" value="0.0"/>
    <PARAM id="evoPointB_31" value="0.0"/>
    <PARAM id="evoPoint_00" value="0.0"/>
    <PARAM id="evoPoint_01" value="0.8210000395774841"/>
    <PARAM id="evoPoint_02" value="0.3210000097751617"/>
    <PARAM id="evoPoint_03" value="0.02200000174343586"/>
    <PARAM id="evoPoint_04" value="0.02700000070035458"/>
    <PARAM id="evoPoint_05" value="0.0430000014603138"/>
    <PARAM id="evoPoint_06" value="0.04900000244379044"/>
    <PARAM id="evoPoint_07" value="0.07600000500679016"/>
    <PARAM id="evoPoint_08" value="0.09800000488758087"/>
    <PARAM id="evoPoint_09" value="0.1140000075101852"/>
    <PARAM id="evoPoint_10" value="0.136000007390976"/>
    <PARAM id="evoPoint_11" value="0.1680000126361847"/>
    <PARAM id="evoPoint_12" value="0.1960000097751617"/>
    <PARAM id="evoPoint_13" value="0.2450000047683716"/>
    <PARAM id="evoPoint_14" value="0.2660000026226044"/>
    <PARAM id="evoPoint_15" value="0.2930000126361847"/>
    <PARAM id="evoPoint_16" value="0.3260000050067902"/>
    <PARAM id="evoPoint_17" value="0.359000027179718"/>
    <PARAM id="evoPoint_18" value="0.3910000324249268"/>
    <PARAM id="evoPoint_19" value="0.4240000247955322"/>
    <PARAM id="evoPoint_20" value="0.4780000150203705"/>
    <PARAM id="evoPoint_21" value="0.5049999952316284"/>
    <PARAM id="evoPoint_22" value="0.5540000200271606"/>
    <PARAM id="evoPoint_23" value="0.609000027179718"/>
    <PARAM id="evoPoint_24" value="0.6580000519752502"/>
    <PARAM id="evoPoint_25" value="0.7170000076293945"/>
    <PARAM id="evoPoint_26" value="0.7610000371932983"/>
    <PARAM id="evoPoint_27" value="0.8040000200271606"/>
    <PARAM id="evoPoint_28" value="0.8480000495910645"/>
    <PARAM id="evoPoint_29" value="0.9130000472068787"/>
    <PARAM id="evoPoint_30" value="0.9620000720024109"/>
    <PARAM id="evoPoint_31" value="1.0"/>
    <PARAM id="evoStepped" value="0.0"/>
    <PARAM id="evoSteppedB" value="0.0"/>
    <PARAM id="evoTime" value="4.0"/>
    <PARAM id="filterAtt" value="0.01000000070780516"/>
    <PARAM id="filterDec" value="0.4999999403953552"/>
    <PARAM id="filterEnvAmt" value="0.0"/>
    <PARAM id="filterFreq" value="20000.0"/>
    <PARAM id="filterLFODep" value="0.0"/>
    <PARAM id="filterPerVoice" value="0.0"/>
    <PARAM id="filterQ" value="0.7100000381469727"/>
    <PARAM id="filterRel" value="0.4999999403953552"/>
    <PARAM id="filterSus" value="0.0"/>
    <PARAM id="frameInterp" value="0.0"/>
    <PARAM id="gain" value="0.7000000476837158"/>
    <PARAM id="glide" value="0.0"/>
    <PARAM id="grit" value="0.0"/>
    <PARAM id="jumpProb" value="0.0"/>
    <PARAM id="mono" value="0.0"/>
    <PARAM id="noiseLevel" value="0.0"/>
    <PARAM id="octaveA" value="0.0"/>
    <PARAM id="octaveB" value="0.0"/>
    <PARAM id="oscMix" value="0.5"/>
    <PARAM id="pitchEnvAmt" value="0.0"/>
    <PARAM id="pitchEnvAtt" value="0.05000000074505806"/>
    <PARAM id="pitchEnvDec" value="0.300000011920929"/>
    <PARAM id="pitchLFO" value="0.0"/>
    <PARAM id="pitchLFORate" value="0.9999999403953552"/>
    <PARAM id="posLFODepth" value="0.0"/>
    <PARAM id="posLFORate" value="0.4999999701976776"/>
    <PARAM id="release" value="0.4999999403953552"/>
    <PARAM id="release2" value="2.467000007629395"/>
    <PARAM id="reverbDamp" value="0.737000048160553"/>
    <PARAM id="reverbSize" value="0.7140000462532043"/>
    <PARAM id="reverbWet" value="0.06400000303983688"/>
    <PARAM id="ringMod" value="0.1150000020861626"/>
    <PARAM id="scanStyle" value="0.0"/>
    <PARAM id="spread" value="0.0"/>
    <PARAM id="stereoPhase" value="0.0"/>
    <PARAM id="stereoWidth" value="0.5"/>
    <PARAM id="sustain" value="0.0"/>
    <PARAM id="sustain2" value="0.0"/>
    <PARAM id="uniDetune" value="0.0"/>
    <PARAM id="velToFrame" value="0.0"/>
    <PARAM id="cycleSizeA"/>
    <PARAM id="cycleSizeB"/>
    <PARAM id="evoBPhaseOff"/>
    <PARAM id="evoRestart"/>
    <PARAM id="evoTimeB"/>
    <PARAM id="frameSnap"/>
    <PARAM id="guiWidth"/>
    <PARAM id="keytrack"/>
    <PARAM id="twAmt"/>
    <PARAM id="twAtt"/>
    <PARAM id="twDec"/>
    <PARAM id="twRel"/>
    <PARAM id="twSus"/>
    <PARAM id="twToFilter"/>
    <PARAM id="twVelAmt"/>
  </Parameters>
</PhizmOscState>)PHZ" },
};
static constexpr int kNumPhizmOscFactoryPresets = (int) (sizeof (kPhizmOscFactoryPresets) / sizeof (kPhizmOscFactoryPresets[0]));
