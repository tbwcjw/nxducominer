<p align="center">
<!-- remove token before push -->
<img width="15%" src="https://raw.githubusercontent.com/tbwcjw/nxducominer/refs/heads/main/assets/Switch_Miner.png">
</p>
    <h2 align="center">nxducominer</h2>
<p align="center">
    A <a href='https://duinocoin.com'>DUCO</a> Miner for the Nintendo Switch.
</p>
<hr>
<h4>Setup</h4>
<ul>
    <li>Unzip <code>[release].zip</code>, copy <code>switch/</code> to the root of your SD card.</li>
    <li id="cfg">Modify <code>config.txt</code> with the node and port to connect to<sup><a href='#node'>*1</a></sup>, your wallet address, your mining key (if applicable), the starting difficulty<sup><a href='#diff'>*2</a></sup>, and a rig_id.</li>
    <li>Launch the miner from HB menu</li>
</ul>
<sup id='node'>*1. <a href='https://server.duinocoin.com/getPool'>https://server.duinocoin.com/getPool</a></sup><br>
<sup id='diff'>*2. <a href='https://github.com/revoxhere/duino-coin/tree/useful-tools#duco-s1-mining'>https://github.com/revoxhere/duino-coin/tree/useful-tools#duco-s1-mining</a></sup>
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
                <li>Handle <code>consoleExit()</code> without error.</li>
                <li>Prevent switch from auto-sleeping in app.</li>
            </td>
            <td>
                <li>Multithreading.</li>
            </td>
            <td>
                <li>Pretty GUI.</li>
            </td>
        </tr>
    </tbody>
</table>
<hr>
<h4>Licenses</h4>
<a href='https://www.gnu.org/licenses/gpl-3.0.en.html'><img src='https://camo.githubusercontent.com/7710eaa5373ee99658cc5c6e389bb88119903cbf92422f24c1e92cd957793e8c/68747470733a2f2f7777772e676e752e6f72672f67726170686963732f67706c76332d3132377835312e706e67'></a><br>
nxducominer is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
