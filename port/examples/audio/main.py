# main.py -- put your code here!from mpython import *
from mpython import *
import network
import audio
from audio import tts, sr

my_wifi = wifi()
my_wifi.connectWiFi("labplus_dev", "helloworld")
# my_wifi.connectWiFi("office", "wearemaker")

def cb(state):
    print("Play state:", state)
    
p = audio.player(cb)
p.set_vol(80)
p.play("http://cdn.makeymonkey.com/test/32_%E6%8B%94%E8%90%9D%E5%8D%9C.mp3")
time.sleep(5)
p.stop()
# # p.play("https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3")
# p.play("file://sd/test.amr")

def rcb():
    print("Record end.")
r = audio.recorder()
r.start("file://sd/1.wav", format=r.WAV, maxtime=5, endcb = rcb)
time.sleep(10)
print("stop rec")
# p.play("file://sd/1.wav")
p.play("file://sd/test.wav")
time.sleep(10)

# audio.set_volume(80)
tts.generate("1月16日上午，国家主席习近平在北京人民大会堂接受18位驻华大使递交国书。人民大会堂北门外，礼兵庄严列队，迎宾号角吹响。使节们相继抵达，穿过旗阵，沿汉白玉台阶拾级而上。在巨幅壁画《江山如此多娇》前，习近平分别接受使节们递交国书，并同他们一一合影。他们是：土耳其驻华大使于纳尔、奥地利驻华大使海沃福、英国驻华大使魏磊、伊拉克驻华大使贝尔瓦利、塞浦路斯驻华大使索菲亚努、毛里求斯驻华大使萧晓山、斯洛伐克驻华大使莱齐亚克、加纳驻华大使邦苏、纳米比亚驻华大使埃姆武拉、韩国驻华大使卢载宪、乌拉圭驻华大使卡夫拉尔、瑞士驻华大使马婷、巴勒斯坦驻华大使贾瓦德、秘鲁驻华大使巴斯克斯、黎巴嫩驻华大使拉德、刚果（布）驻华大使马米纳、莱索托驻华大使林梅、缅甸驻华大使佐温敏。仪式结束后，习近平在北京厅对使节们发表讲话。习近平欢迎使节们来华履职，请他们转达对各自国家领导人和人民的良好祝愿，希望使节们多到各地走走看看，全面深入了解真实立体的中国，为深化中国同各国友谊和合作积极贡献力量。习近平强调，今天的中国，正在中国式现代化新征程上勇毅前行。“十四五”规划圆满收官，经济总量连续跨越，科技创新成果丰硕，绿水青山成为亮丽底色，民生答卷温暖人心。“十五五”规划建议为未来5年中国经济社会发展擘画蓝图，也为世界提供了一份“机遇清单”。中国开放的大门将越开越大，将通过高质量发展为世界注入更多确定性和新动能，实现共同发展、美美与共。习近平指出，今天的世界，百年变局加速演进，国际形势变乱交织，全球性挑战更加突出。分裂对抗、零和博弈注定没有出路，让世界重回丛林法则不得人心，同球共济、团结合作才是唯一正确选择。我提出全球治理倡议，旨在推动构建更加公正合理的全球治理体系。中国将始终站在历史正确一边，以人民之心为心、以天下之利为利，同各国一道推动构建人类命运共同体。")
tts.generate('中华人民共和国成立于一九四九年十月一日，每年的这一天是中华人民共和国的国庆节。')

# r = audio.recorder()
# r.start("file://sd/1.wav", format=r.WAV, maxtime=5, endcb = rcb)
# time.sleep(10)