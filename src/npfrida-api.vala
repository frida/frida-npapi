namespace NPFrida {
	[DBus (name = "com.appspot.npfrida.RootApi")]
	public interface RootApi : Object {
		public abstract async string enumerate_devices () throws Error;
		public abstract async string enumerate_processes (uint device_id) throws Error;
		public abstract async void attach_to (uint device_id, uint pid, string source) throws Error;
		public abstract async void post_message (uint device_id, uint pid, string message) throws Error;
		public abstract async void detach_from (uint device_id, uint pid) throws Error;

		public signal void devices_changed ();
		public signal void detach (uint device_id, uint pid);
		public signal void message (uint device_id, uint pid, string text, Variant? data);
	}

	public class Dispatcher : GLib.Object {
		protected unowned NPFrida.Object target_object;
		protected DBusMethodInfo ** methods;
		protected DBusInterfaceMethodCallFunc dispatch_func;

		public Dispatcher.for_object (NPFrida.Object obj) {
			init_with_object (obj);
		}

		private extern void init_with_object (NPFrida.Object obj);

		public bool has_method (string name) {
			return find_method_by_name (name) != null;
		}

		public void validate_invoke (string name, Variant? args) throws IOError {
			var method = find_method_by_name (name);
			if (method == null)
				throw new IOError.NOT_FOUND ("no such method");

			validate_argument_list (args, method);
		}

		public async Variant? invoke (string name, Variant? args) throws IOError {
			var method = find_method_by_name (name);
			var parameters_in = coerce_argument_list (args, method);
			var parameters_out = yield do_invoke (method, parameters_in);
			assert (parameters_out.n_children () <= 1);
			if (parameters_out.n_children () == 0)
				return null;
			return parameters_out.get_child_value (0);
		}

		private extern async Variant? do_invoke (DBusMethodInfo * method, Variant parameters) throws IOError;

		private DBusMethodInfo * find_method_by_name (string name) {
			if (name.length == 0)
				return null;

			var dbus_name = new StringBuilder ();
			dbus_name.append_unichar (name[0].toupper ());
			dbus_name.append (name.substring (1));

			for (DBusMethodInfo ** method = methods; *method != null; method++) {
				if ((*method)->name == dbus_name.str)
					return *method;
			}

			return null;
		}

		private void validate_argument_list (Variant args, DBusMethodInfo * method) throws IOError {
			var actual_arg_count = (int) args.n_children ();
			var expected_arg_count = method->in_args.length;
			if (actual_arg_count != expected_arg_count)
				throw new IOError.INVALID_ARGUMENT ("argument count mismatch");

			for (var i = 0; i != expected_arg_count; i++) {
				var arg = args.get_child_value (i);

				var actual_type = arg.get_type ();
				var expected_type = new VariantType (method->in_args[i].signature);

				bool types_are_compatible;
				if (actual_type.equal (expected_type))
					types_are_compatible = true;
				else
					types_are_compatible = is_numeric (expected_type) && is_numeric (actual_type);

				if (!types_are_compatible)
					throw new IOError.INVALID_ARGUMENT ("argument type mismatch");
			}
		}

		private Variant coerce_argument_list (Variant args, DBusMethodInfo * method) {
			var builder = new VariantBuilder (VariantType.TUPLE);
			var i = 0;
			foreach (var ai in method->in_args) {
				var arg = args.get_child_value (i);

				var actual_type = arg.get_type ();
				var expected_type = new VariantType (method->in_args[i].signature);

				Variant coerced_arg;
				if (actual_type.equal (expected_type)) {
					coerced_arg = arg;
				} else {
					var val = double.parse (arg.print (false));
					if (expected_type.equal (VariantType.INT32))
						coerced_arg = new Variant.int32 ((int32) val);
					else if (expected_type.equal (VariantType.UINT32))
						coerced_arg = new Variant.uint32 ((uint32) val);
					else if (expected_type.equal (VariantType.DOUBLE))
						coerced_arg = new Variant.double (val);
					else if (expected_type.equal (VariantType.INT64))
						coerced_arg = new Variant.int64 ((int64) val);
					else if (expected_type.equal (VariantType.UINT64))
						coerced_arg = new Variant.uint64 ((uint64) val);
					else if (expected_type.equal (VariantType.INT16))
						coerced_arg = new Variant.int16 ((int16) val);
					else if (expected_type.equal (VariantType.UINT16))
						coerced_arg = new Variant.uint16 ((uint16) val);
					else if (expected_type.equal (VariantType.BYTE))
						coerced_arg = new Variant.byte ((uchar) val);
					else
						assert_not_reached ();
				}
				builder.add_value (coerced_arg);

				i++;
			}

			return builder.end ();
		}

		private bool is_numeric (VariantType type) {
			VariantType[] numeric_types = {
				VariantType.INT32,
				VariantType.UINT32,
				VariantType.DOUBLE,
				VariantType.INT64,
				VariantType.UINT64,
				VariantType.INT16,
				VariantType.UINT16,
				VariantType.BYTE
			};

			foreach (var numeric_type in numeric_types) {
				if (type.equal (numeric_type))
					return true;
			}

			return false;
		}
	}
}
