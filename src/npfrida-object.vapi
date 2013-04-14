[CCode (cprefix = "NPFrida", lower_case_cprefix = "npfrida_")]
namespace NPFrida {
	[CCode (cheader_filename = "npfrida-object.h")]
	public abstract class Object : GLib.Object {
		[CCode (has_construct_function = false)]
		protected Object ();

		protected abstract async void destroy ();
	}
}
