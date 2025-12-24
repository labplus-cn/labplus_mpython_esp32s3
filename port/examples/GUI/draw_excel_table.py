from mpython import *

# 1. 创建表格对象     
table = MicroExcelTable()
# table = MicroExcelTable(y0=10, cols=2, rows=3, cell_width=100,)

display.clear()
# 2. 绘制表头（仅调用一次）
col_headers = ["温度", "湿度", "光线值"]   # 列表头
row_headers = ["位置1", "位置2", "位置3", "位置4"] # 列表头
# col_headers = ["温度", "湿度"]   # 列表头
# row_headers = ["位置1", "位置2", "位置3"] # 列表头
table.draw_headers(col_headers, row_headers)

# 3. 初始化数据（整区更新）
table.update_data([
    ["25℃", "60%", "200"],
    ["26℃", "59%", "700"],
    ["24℃", "61%", "1210"],
    ["25℃", "61%", "120"],
])

# table.update_data([
#     ["25℃", "60%"],
#     ["26℃", "59%"],
#     ["24℃", "61%"],
# ])

display.show()

# 4. 模拟实时更新单个单元格（核心演示）
count = 0
while True:
    # 仅更新第0行第0列的温度值
    table.update_cell(row=0, col=0, text=f"{(25+count) % 99}℃")
    # 仅更新第2行第3列的电量值
    table.update_cell(row=2, col=2, text=f"{count % 1024}")
    display.show()
    count += 1
    time.sleep(1)