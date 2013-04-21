namespace NPFrida {
	public class Root : Object, RootApi {
		private Frida.DeviceManager manager = new Frida.DeviceManager ();
		private Gee.ArrayList<Entry> entries = new Gee.ArrayList<Entry> ();

		construct {
			manager.changed.connect (on_changed);
		}

		protected override async void destroy () {
			yield manager.close ();
			manager = null;
		}

		public async string enumerate_devices () throws Error {
			var builder = new Json.Builder ();
			builder.begin_array ();
			var devices = yield manager.enumerate_devices ();
			var count = devices.size ();
			for (var i = 0; i != count; i++) {
				var device = devices.get (i);
				builder.begin_object ();
				builder.set_member_name ("id").add_int_value (device.id);
				builder.set_member_name ("name").add_string_value (device.name);
				append_icon ("icon", device.icon, builder);
				builder.set_member_name ("type").add_string_value (device_type_to_string (device.dtype));
				builder.end_object ();
			}
			builder.end_array ();
			var generator = new Json.Generator ();
			generator.set_root (builder.get_root ());
			return generator.to_data (null);
		}

		public async string enumerate_processes (uint device_id) throws Error {
			var device = yield get_device_by_id (device_id);
			var builder = new Json.Builder ();
			builder.begin_array ();
			var processes = yield device.enumerate_processes ();
			var count = processes.size ();
			for (var i = 0; i != count; i++) {
				var process = processes.get (i);
				builder.begin_object ();
				builder.set_member_name ("pid").add_int_value (process.pid);
				builder.set_member_name ("name").add_string_value (process.name);
				append_icon ("small_icon", process.small_icon, builder);
				append_icon ("large_icon", process.large_icon, builder);
				builder.end_object ();
			}
			builder.end_array ();
			var generator = new Json.Generator ();
			generator.set_root (builder.get_root ());
			return generator.to_data (null);
		}

		private static string device_type_to_string (Frida.DeviceType type) {
			switch (type) {
				case Frida.DeviceType.LOCAL:
					return "local";
				case Frida.DeviceType.TETHER:
					return "tether";
				case Frida.DeviceType.REMOTE:
					return "remote";
				default:
					assert_not_reached ();
			}
		}

		private static void append_icon (string member_name, Frida.Icon? icon, Json.Builder builder) {
			if (icon == null)
				return;
			var image = builder.set_member_name (member_name);
			image.begin_object ();
			image.set_member_name ("width").add_int_value (icon.width);
			image.set_member_name ("height").add_int_value (icon.height);
			image.set_member_name ("rowstride").add_int_value (icon.rowstride);
			image.set_member_name ("pixels").add_string_value (Base64.encode (icon.pixels));
			image.end_object ();
		}

		public async void attach_to (uint device_id, uint pid, string source) throws Error {
			var entry = yield get_entry (device_id, pid, false);
			yield entry.load_script (source);
		}

		public async void post_message (uint device_id, uint pid, string message) throws Error {
			var entry = yield get_entry (device_id, pid, true);
			yield entry.post_message (message);
		}

		public async void detach_from (uint device_id, uint pid) throws Error {
			var entry = yield get_entry (device_id, pid, true);
			yield entry.unload_script ();
			yield entry.session.detach ();
		}

		private void on_changed () {
			devices_changed ();
		}

		private async Entry get_entry (uint device_id, uint pid, bool must_exist) throws Error {
			var device = yield get_device_by_id (device_id);
			var session = yield device.attach (pid);
			foreach (var entry in entries) {
				if (entry.session == session)
					return entry;
			}
			if (must_exist)
				throw new IOError.FAILED ("not attached");
			var e = new Entry (this, device, session);
			entries.add (e);
			return e;
		}

		private void _release_entry (Entry entry) {
			detach (entry.device.id, entry.session.pid);
			entries.remove (entry);
		}

		private async Frida.Device get_device_by_id (uint device_id) throws Error {
			var devices = yield manager.enumerate_devices ();
			var count = devices.size ();
			for (var i = 0; i < count; i++) {
				var device = devices.get (i);
				if (device.id == device_id)
					return device;
			}
			throw new IOError.FAILED ("invalid device id");
		}

		private class Entry : GLib.Object {
			public Frida.Device device {
				get;
				private set;
			}

			public Frida.Session session {
				get;
				private set;
			}

			private weak Root parent;
			private Frida.Script script;

			public Entry (Root parent, Frida.Device device, Frida.Session session) {
				this.parent = parent;
				this.device = device;
				this.session = session;
				session.detached.connect (on_session_detached);
			}

			public async void load_script (string source) throws Error {
				yield unload_script ();
				var s = yield session.create_script (source);
				s.message.connect (on_script_message);
				yield s.load ();
				script = s;
			}

			public async void unload_script () throws Error {
				if (script == null)
					return;
				yield script.unload ();
				script = null;
			}

			public async void post_message (string message) throws Error {
				if (script == null)
					throw new IOError.FAILED ("no script loaded");
				yield script.post_message (message);
			}

			private void on_session_detached () {
				parent._release_entry (this);
			}

			private void on_script_message (string message, uint8[] data) {
				Variant data_value = null;
				if (data.length > 0) {
					void * data_copy_raw = Memory.dup (data, data.length);
					unowned uint8[data.length] data_copy = (uint8[]) data_copy_raw;
					data_copy.length = data.length;
					data_value = Variant.new_from_data<uint8[]> (new VariantType ("ay"), data_copy, true);
				}
				parent.message (device.id, session.pid, message, data_value);
			}
		}
	}
}
