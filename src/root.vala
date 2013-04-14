namespace CloudSpy {
	public class Root : Object, RootApi {
		private Gee.ArrayList<Device> devices = new Gee.ArrayList<Device> ();
		private uint last_device_id = 1;

#if !LINUX
		private Zed.FruityHostSessionBackend fruity;
#endif

		protected override async void destroy () {
			foreach (var device in devices) {
				yield device.close ();
			}
			devices.clear ();

#if !LINUX
			if (fruity != null) {
				yield fruity.stop ();
				fruity = null;
			}
#endif
		}

		private async void ensure_devices () throws IOError {
			if (devices.size > 0)
				return;
			var local = new LocalDevice (last_device_id++);
			yield local.open ();
			add_device (local);

#if !LINUX
			fruity = new Zed.FruityHostSessionBackend ();
			fruity.provider_available.connect ((provider) => {
				var device = new Device (last_device_id++, provider.name, provider.kind, provider);
				add_device (device);
				devices_changed ();
			});
			fruity.provider_unavailable.connect ((provider) => {
				foreach (var device in devices) {
					if (device.provider == provider) {
						device.close ();
						devices.remove (device);
						break;
					}
				}
				devices_changed ();
			});
			yield fruity.start ();
#endif
		}

		private void add_device (Device device) {
			devices.add (device);

			var device_id = device.id;
			device.detach.connect ((pid) => detach (device_id, pid));
			device.message.connect ((pid, text, data) => message (device_id, pid, text, data));
		}

		public async string enumerate_devices () throws IOError {
			yield ensure_devices ();

			var builder = new Json.Builder ();
			builder.begin_array ();
			foreach (var device in devices) {
				builder.begin_object ();
				builder.set_member_name ("id").add_int_value (device.id);
				builder.set_member_name ("name").add_string_value (device.name);
				builder.set_member_name ("type").add_string_value (device.kind);
				builder.end_object ();
			}
			builder.end_array ();
			var generator = new Json.Generator ();
			generator.set_root (builder.get_root ());
			return generator.to_data (null);
		}

		public async string enumerate_processes (uint device_id) throws IOError {
			return yield get_device_by_id (device_id).enumerate_processes ();
		}

		public async void attach_to (uint device_id, uint pid, string source) throws IOError {
			yield get_device_by_id (device_id).attach_to (pid, source);
		}

		public async void post_message (uint device_id, uint pid, string message) throws IOError {
			yield get_device_by_id (device_id).post_message (pid, message);
		}

		public async void detach_from (uint device_id, uint pid) throws IOError {
			yield get_device_by_id (device_id).detach_from (pid);
		}

		private Device get_device_by_id (uint device_id) throws IOError {
			foreach (var device in devices) {
				if (device.id == device_id)
					return device;
			}

			throw new IOError.FAILED ("invalid device id");
		}

		protected class Device {
			public uint id {
				get;
				private set;
			}

			public string name {
				get;
				private set;
			}

			public string kind {
				get;
				private set;
			}

			public Zed.HostSessionProvider provider {
				get;
				private set;
			}

			public signal void detach (uint pid);
			public signal void message (uint pid, string text, Variant? data);

			private Zed.HostSession host_session;
			private Gee.HashMap<uint, Zed.AgentSession> agent_by_pid = new Gee.HashMap<uint, Zed.AgentSession> ();
			private Gee.HashMap<uint, uint> pid_by_agent_id = new Gee.HashMap<uint, uint> ();
			private Gee.HashMap<uint, uint> script_by_pid = new Gee.HashMap<uint, uint> ();

			public Device (uint id, string name, Zed.HostSessionProviderKind kind, Zed.HostSessionProvider provider) {
				this.id = id;
				this.name = name;
				switch (kind) {
					case Zed.HostSessionProviderKind.LOCAL_SYSTEM:
						this.kind = "local";
						break;
					case Zed.HostSessionProviderKind.LOCAL_TETHER:
						this.kind = "tether";
						break;
					case Zed.HostSessionProviderKind.REMOTE_SYSTEM:
						this.kind = "remote";
						break;
				}
				this.provider = provider;

				provider.agent_session_closed.connect ((sid, error) => {
					uint pid;
					if (pid_by_agent_id.unset (sid.handle, out pid)) {
						agent_by_pid.unset (pid);
						if (script_by_pid.unset (pid))
							detach (pid);
					}
				});
			}

			public virtual async void close () {
				var pids = script_by_pid.keys.to_array ();
				var agents = agent_by_pid.values.to_array ();
				agent_by_pid.clear ();
				pid_by_agent_id.clear ();
				script_by_pid.clear ();

				foreach (var pid in pids)
					detach (pid);

				foreach (var agent in agents) {
					try {
						yield agent.close ();
					} catch (IOError e) {
					}
				}

				host_session = null;
			}

			public async string enumerate_processes () throws IOError {
				var host = yield obtain_host_session ();

				var builder = new Json.Builder ();
				builder.begin_array ();
				foreach (var pi in yield host.enumerate_processes ()) {
					builder.begin_object ();
					builder.set_member_name ("pid").add_int_value (pi.pid);
					builder.set_member_name ("name").add_string_value (pi.name);
					append_image ("small_icon", pi.small_icon, builder);
					append_image ("large_icon", pi.large_icon, builder);
					builder.end_object ();
				}
				builder.end_array ();
				var generator = new Json.Generator ();
				generator.set_root (builder.get_root ());
				return generator.to_data (null);
			}

			private static void append_image (string member_name, Zed.ImageData data, Json.Builder builder) {
				if (data.width == 0)
					return;
				var image = builder.set_member_name (member_name);
				image.begin_object ();
				image.set_member_name ("width").add_int_value (data.width);
				image.set_member_name ("height").add_int_value (data.height);
				image.set_member_name ("rowstride").add_int_value (data.rowstride);
				image.set_member_name ("pixels").add_string_value (data.pixels);
				image.end_object ();
			}

			public async void attach_to (uint pid, string source) throws IOError {
				var agent = yield obtain_agent_session (pid);
				var script = yield agent.create_script (source);
				yield agent.load_script (script);

				if (script_by_pid.has_key (pid)) {
					try {
						yield detach_from (pid);
					} catch (IOError e) {
					}
				}

				script_by_pid[pid] = script.handle;
			}

			public async void post_message (uint pid, string message) throws IOError {
				if (!script_by_pid.has_key (pid))
					throw new IOError.FAILED ("no script associated with pid %u".printf (pid));
				var agent = yield obtain_agent_session (pid);
				yield agent.post_message_to_script (Zed.AgentScriptId (script_by_pid[pid]), message);
			}

			public async void detach_from (uint pid) throws IOError {
				uint handle;
				if (!script_by_pid.unset (pid, out handle))
					throw new IOError.FAILED ("no script associated with pid %u".printf (pid));

				var agent = yield obtain_agent_session (pid);
				yield agent.destroy_script (Zed.AgentScriptId (handle));
			}

			protected async Zed.HostSession obtain_host_session () throws IOError {
				if (host_session == null) {
					host_session = yield provider.create ();
				}

				return host_session;
			}

			protected async Zed.AgentSession obtain_agent_session (uint pid) throws IOError {
				yield obtain_host_session ();

				var agent = agent_by_pid[pid];
				if (agent == null) {
					var agent_id = yield host_session.attach_to (pid);
					agent = yield provider.obtain_agent_session (agent_id);
					agent.message_from_script.connect ((sid, text, data) => {
						Variant data_value = null;
						if (data.length > 0) {
							void * data_copy_raw = Memory.dup (data, data.length);
							unowned uint8[data.length] data_copy = (uint8[]) data_copy_raw;
							data_copy.length = data.length;
							data_value = Variant.new_from_data<uint8[]> (new VariantType ("ay"), data_copy, true);
						}
						message (pid, text, data_value);
					});
					pid_by_agent_id[agent_id.handle] = pid;
					agent_by_pid[pid] = agent;
				}

				return agent;
			}
		}

		protected class LocalDevice : Device {
#if !WINDOWS
			private Server server;

			private Zed.TcpHostSessionProvider local_provider;
#else
			private Zed.WindowsHostSessionProvider local_provider;
#endif

			public LocalDevice (uint id) throws IOError {
#if !WINDOWS
				var s = new Server ();
				var p = new Zed.TcpHostSessionProvider.for_address (s.address);
#else
				var p = new Zed.WindowsHostSessionProvider ();
#endif
				base (id, "Local System", Zed.HostSessionProviderKind.LOCAL_SYSTEM, p);

#if !WINDOWS
				server = s;
#endif
				local_provider = p;
			}

			public async void open () throws IOError {
				yield base.obtain_host_session ();
			}

			public override async void close () {
				yield base.close ();

				yield local_provider.close ();
				local_provider = null;

#if !WINDOWS
				server.destroy ();
				server = null;
#endif
			}
		}

#if !WINDOWS
		protected class Server {
			private TemporaryFile executable;

			public string address {
				get;
				private set;
			}

			private const string SERVER_ADDRESS_TEMPLATE = "tcp:host=127.0.0.1,port=%u";

			public Server () throws IOError {
				var blob = CloudSpy.Data.get_zed_server_blob ();
				executable = new TemporaryFile.from_stream ("server", new MemoryInputStream.from_data (blob.data, null));
				try {
					executable.file.set_attribute_uint32 (FILE_ATTRIBUTE_UNIX_MODE, 0755, FileQueryInfoFlags.NONE);
				} catch (Error e) {
					throw new IOError.FAILED (e.message);
				}

				address = SERVER_ADDRESS_TEMPLATE.printf (get_available_port ());

				try {
					string[] argv = new string[] { executable.file.get_path (), address };
					Pid child_pid;
					Process.spawn_async (null, argv, null, 0, null, out child_pid);
				} catch (SpawnError e) {
					executable.destroy ();
					throw new IOError.FAILED (e.message);
				}
			}

			public void destroy () {
				executable.destroy ();
			}

			private uint get_available_port () {
				uint port = 27042;

				bool found_available = false;
				var loopback = new InetAddress.loopback (SocketFamily.IPV4);
				var address_in_use = new IOError.ADDRESS_IN_USE ("");
				while (!found_available) {
					try {
						var socket = new Socket (SocketFamily.IPV4, SocketType.STREAM, SocketProtocol.TCP);
						socket.bind (new InetSocketAddress (loopback, (uint16) port), false);
						socket.close ();
						found_available = true;
					} catch (Error e) {
						if (e.code == address_in_use.code)
							port--;
						else
							found_available = true;
					}
				}

				return port;
			}
		}

		protected class TemporaryFile {
			public File file {
				get;
				private set;
			}

			public TemporaryFile.from_stream (string name, InputStream istream) throws IOError {
				this.file = File.new_for_path (Path.build_filename (Environment.get_tmp_dir (), "cloud-spy-%p-%u-%s".printf (this, Random.next_int (), name)));

				try {
					var ostream = file.create (FileCreateFlags.NONE, null);

					var buf_size = 128 * 1024;
					var buf = new uint8[buf_size];

					while (true) {
						var bytes_read = istream.read (buf);
						if (bytes_read == 0)
							break;
						buf.resize ((int) bytes_read);

						size_t bytes_written;
						ostream.write_all (buf, out bytes_written);
					}

					ostream.close (null);
				} catch (Error e) {
					throw new IOError.FAILED (e.message);
				}
			}

			~TemporaryFile () {
				destroy ();
			}

			public void destroy () {
				try {
					file.delete (null);
				} catch (Error e) {
				}
			}
		}
#endif
	}
}
