[CCode (cprefix = "CloudSpy", lower_case_cprefix = "cloud_spy_")]
namespace CloudSpy {
	[CCode (cheader_filename = "cloud-spy-object.h")]
	public abstract class Object : GLib.Object {
		[CCode (has_construct_function = false)]
		protected Object ();

		protected abstract async void destroy ();
	}
}
