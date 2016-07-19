# switchvmx
This needs a module that supports switching procs into vmx root mode
to compile switch binary do
$make 

Run switch as a background process
$./switch -d

To switch procs in the range 1-2 into vmx non-root mode, do
./switch in 1-2

This sends procs 1 and 2 into vmx mode

To switch procs in the range 1-2 into vmx root mode, do
./switch out 1-2
