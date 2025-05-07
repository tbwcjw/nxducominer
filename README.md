<p align="center">
<img width="15%" src="assets/Switch_Miner.png">
</p>
    <h2 align="center">nxducominer</h2>
<p align="center">
    A <a href='https://duinocoin.com'>DUCO</a> Miner for the Nintendo Switch.
</p>
<hr>
<p align="center">
    <img src="https://github.com/tbwcjw/nxducominer/actions/workflows/c.yml/badge.svg?event=push">
</p>
<h4>Setup</h4>
<ul>
    <li>Unzip <code>[release].zip</code>, copy <code>switch/</code> to the root of your SD card.</li>
    <li id="cfg">Set <code>config.txt</code> with the node and port to connect to<sup><a href='#node'>*1</a></sup>, your wallet address, your mining key (if applicable), the starting difficulty<sup><a href='#diff'>*2</a></sup>, a rig_id,
    and cpu_boost<sup><a href='#boost'>*3</a></sup>.</li>
    <li>Launch the miner from HB menu</li>
</ul>
<sup id='node'>*1. <a href='https://server.duinocoin.com/getPool'>https://server.duinocoin.com/getPool</a></sup><br>
<sup id='diff'>*2. <a href='https://github.com/revoxhere/duino-coin/tree/useful-tools#duco-s1-mining'>https://github.com/revoxhere/duino-coin/tree/useful-tools#duco-s1-mining</a></sup><br>
<sup id='diff'>*3. </a><code>true</code> or <code>false</code>. Using <a href='https://switchbrew.github.io/libnx/apm_8h.html#a5690c3a786c3bee6ef93f5db5354e080'>ApmCpuBoostMode</a> in mode <code>ApmCpuBoostMode_FastLoad</code>. Nearly doubles hashrate performance, but the switch does get quite hot over time.</sup>
<hr>
<h4>Building</h4>
<ul>
    <li>Install <a href='https://devkitpro.org'>devkitpro</a> with at least <code>switch-dev</code> using the <a href='https://devkitpro.org/wiki/Getting_Started'>Getting Started</a> guide. For building releases you will need <code>zip</code>.
    <li>Clone <a href='https://github.com/tbwcjw/nxducominer.git'>https://github.com/tbwcjw/nxducominer.git</a>.
    <li>Copy <code>config.sample.txt</code> to <code>config.txt</code> and fill in the fields<sup><a href='#cfg'>*</a></sup>.
    <li>
        <code>make (all)</code> - build the application and generate a release.
        <br>
        <code>make build</code> - build the application.
        <br>
        <code>make release</code> - builds and generate a release.
        <br>
        <code>make clean</code> - removes build/build data. does not remove releases
    </li>
    <li>Use NXLink to send <code>application.nro</code> to the switch, or follow the setup guide above for a release.</li>
</ul>
<hr>
<h4>Roadmap</h4>
<table>
    <thead>
        <tr>
            <th>Short term</th>
            <th>Mid term</th>
            <th>Long term</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td>
                <li>Automatically select best node.</li>
                <li>Better error handling.</li>
                <li><s>Reconnect to node if connection lost.</s></li>
                <li><s>Handle <code>consoleExit()</code> without error.</s></li>
                <li><s>Prevent switch from auto-sleeping in app.</s></li>
            </td>
            <td>
                <li>Web dashboard, like the ESP32 miner</li>
                <li>Multithreading.</li>
            </td>
            <td>
                <li>Pretty GUI.</li>
            </td>
        </tr>
    </tbody>
</table>
<hr>
<h4>Screenshots</h4>
<p align="center">
<img src="assets/nxducominer_screenshot.png">
</p>
<hr>
<h4>Licenses</h4>
<a href='https://www.gnu.org/licenses/gpl-3.0.en.html'><img src='https://camo.githubusercontent.com/7710eaa5373ee99658cc5c6e389bb88119903cbf92422f24c1e92cd957793e8c/68747470733a2f2f7777772e676e752e6f72672f67726170686963732f67706c76332d3132377835312e706e67'></a><br>
nxducominer is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
