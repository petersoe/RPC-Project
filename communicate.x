program COMMUNICATE_PROG {
	version COMMUNICATE_VERSION {
                bool ping () = 1;
                bool join (string IP, int Port) = 2;
		bool leave (string IP, int Port) = 3;
		bool subscribe(string IP, int Port, string Article) = 4;
		bool unsubscribe (string IP, int Port, string Article) = 5;
		bool publish (string IP, int Port, string Article) = 6;
                bool update (string IP, int Port) = 7;
	} = 203;
} = 0x20000001;
