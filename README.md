# 冰晶蓝玫瑰 · Ice Crystal Rose

一朵由数万个发光粒子构成的 3D 旋转冰晶蓝玫瑰，使用 Windows 自带 OpenGL（固定管线）渲染，**零第三方依赖**，VS2022 直接编译运行。

## 效果

- 3D 粒子玫瑰持续绕轴旋转（22°/s），带俯仰摆动与呼吸光效
- 结构：螺旋冰晶花心 + 三层共 26 片花瓣（内层半合苞 / 中层展开 / 外层杯形卷边）+ 花蕊 + 5 片冰绿萼片 + S 形花茎 + 4 片叶 + 170 颗闪烁星光
- 半透明标准混合渲染，前后遮挡层次分明；深空蓝背景
- 标题栏实时显示 FPS

## 构建

Visual Studio 2022（v143 工具集）打开 `冰晶蓝玫瑰.sln`，选择 Release | x64 按 F5 即可。

或命令行：

```
MSBuild 冰晶蓝玫瑰.sln /t:Build /p:Configuration=Release /p:Platform=x64
```

## 运行

```
冰晶蓝玫瑰.exe                 # 动画模式（ESC 或关闭窗口退出）
冰晶蓝玫瑰.exe -shot a.bmp     # 渲染一帧并保存为 BMP
冰晶蓝玫瑰.exe -shot a.bmp 35  # 指定偏航角（度）
```

## 目录结构

```
冰晶蓝玫瑰.sln
冰晶蓝玫瑰/
  ├─ 冰晶蓝玫瑰.cpp            # 全部源码（单文件）
  ├─ 冰晶蓝玫瑰.vcxproj
  └─ 冰晶蓝玫瑰.vcxproj.filters
```

预览图可自行导出：

```
冰晶蓝玫瑰.exe -shot 预览_35.bmp 35
冰晶蓝玫瑰.exe -shot 预览_130.bmp 130
冰晶蓝玫瑰.exe -shot 预览_220.bmp 220
```

## 说明

- 工程文件与经典 EasyX 粒子爱心项目同风格（单文件源码 + VS 工程）
- 使用 Win32 + OpenGL 1.x 固定管线，不依赖 EasyX / DirectX / 第三方库
- 粒子约 3 万个，深度排序 + 分桶绘制，常规显卡 60 FPS
