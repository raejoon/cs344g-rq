sudo modprobe dccp
sudo sysctl -w net.dccp.default.seq_window=1000
sudo sysctl -w net.dccp.default.rx_ccid=2
sudo sysctl -w net.dccp.default.tx_ccid=2
