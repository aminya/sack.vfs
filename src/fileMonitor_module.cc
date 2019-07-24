
#include "global.h"


static Persistent<Function> constructor;

enum eventType {
	FileMonitor_Event_Change,
	FileMonitor_Event_Close,

};

struct changeEvent {
	enum eventType event;
	struct fileEvent {
		CTEXTSTR path; 
		uint64_t size; 
		uint64_t time; 
		LOGICAL bCreated; 
		LOGICAL bDirectory; 
		LOGICAL bDeleted;
	} file;

};

struct changeTracker {
	uv_async_t async; // keep this instance around for as long as we might need to do the periodic callback
	Persistent<Function> cb;
	PLINKQUEUE events;
};


class monitorWrapper : public node::ObjectWrap {
public:
	PLIST trackers;
	PMONITOR monitor;

	~monitorWrapper() {

		DeleteList( &trackers );
		EndMonitor( this->monitor );
	}

	void monitorWrapSelf( Isolate* isolate, monitorWrapper* _this, Local<Object> into ) {
		_this->Wrap( into );
		//_this->state.Reset( isolate, into );
	}

};

static void monitorAsyncMsg( uv_async_t* handle ) {
	changeTracker * changes = (changeTracker*)handle->data;
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	HandleScope scope( isolate );
	Local<Context> context = isolate->GetCurrentContext();
	struct changeEvent *event;
	Local<Value> argv[1];
	Local<Object> o; 
	while( event = (struct changeEvent*)DequeLink( &changes->events ) ) {
		switch( event->event ) {
		case FileMonitor_Event_Change:

			o = Object::New( isolate );
			SET( o, "path", localString( isolate, event->file.path, StrLen( event->file.path ) ) );
			SET( o, "size", Number::New( isolate, event->file.size ) );
			//SET( o, "time", Date( event->file.time ) );
			SET( o, "created", event->file.bCreated?True(isolate):False(isolate) );
			SET( o, "directory", event->file.bDirectory?True(isolate):False(isolate) );
			SET( o, "deleted", event->file.bDeleted?True(isolate):False(isolate) );
			argv[0] = o;
			changes->cb.Get( isolate )->Call( context, Null(isolate), 1, argv );
			Deallocate( char *, event->file.path );
			break;
		case FileMonitor_Event_Close:
			DeleteLinkQueue( &changes->events );
			uv_close( (uv_handle_t*)&changes->async, NULL );
			Release( changes );
			break;
		}
		Release( event );
	}

}

static int invokeEvent( uintptr_t psv
	, CTEXTSTR path, uint64_t size
	, uint64_t time
	, LOGICAL bCreated, LOGICAL bDirectory
	, LOGICAL bDeleted ) {
	changeTracker *tracker = (changeTracker*)psv;
	struct changeEvent *event = NewArray( struct changeEvent, 1 );
	event->event = FileMonitor_Event_Change;
	event->file.path = StrDup( path );
	event->file.size = size;
	event->file.time = time;
	event->file.bCreated = bCreated;
	event->file.bDirectory = bDirectory;
	event->file.bDeleted = bDeleted;

	EnqueLink( &tracker->events, event );
	uv_async_send( &tracker->async );

	return 1; // dispatch next
}


static void addMonitorFilter( const v8::FunctionCallbackInfo<Value>& args ) {
	monitorWrapper*me = monitorWrapper::Unwrap<monitorWrapper>( args.This() );
	Isolate *isolate = args.GetIsolate();
	Local<Function> cb = Local<Function>::Cast( args[1] );

	changeTracker *tracker = new changeTracker();
	String::Utf8Value mask( args[0]->ToString() );

	tracker->cb.Reset( isolate, cb );

	AddLink( &me->trackers, tracker );
	uv_async_init( uv_default_loop(), &tracker->async, monitorAsyncMsg );
 	tracker->async.data = tracker;

	//AddFileChangeCallback( me->monitor, mask, callback,
	AddExtendedFileChangeCallback( me->monitor, *mask, invokeEvent, (uintptr_t)tracker );


}
static monitorWrapper* newMonitor( char *path, int delay ) {
	monitorWrapper* monitor = new monitorWrapper();
	monitor->monitor = MonitorFiles( path, delay );


	return monitor;
}



static void makeNewMonitor( const FunctionCallbackInfo<Value>& args ) {
	Isolate *isolate = args.GetIsolate();
	if( args.IsConstructCall() ) {
		int defaultDelay = 0;
		String::Utf8Value path( args[0]->ToString() );
		if( args.Length() > 1 ) {
			defaultDelay = args[1]->NumberValue();
		}
		monitorWrapper* obj = newMonitor( *path, defaultDelay );
		obj->monitorWrapSelf( isolate, obj, args.This() );
		args.GetReturnValue().Set( args.This() );

	}
	else {
		// Invoked as plain function `MyObject(...)`, turn into construct call.
		Local<Function> cons = Local<Function>::New( isolate, constructor );
		Local<Value> *passArgs = new Local<Value>[args.Length()];
		int n;
		for( n = 0; n < args.Length(); n++ )
			passArgs[n] = args[n];
		args.GetReturnValue().Set( cons->NewInstance( isolate->GetCurrentContext(), n, passArgs ).ToLocalChecked() );
		delete passArgs;
	}
}


void fileMonitorInit( Isolate* isolate, Handle<Object> exports ) {
	Local<FunctionTemplate> monitorTemplate;

	monitorTemplate = FunctionTemplate::New( isolate, makeNewMonitor );
	monitorTemplate->SetClassName( String::NewFromUtf8( isolate, "sack.FileMonitor", v8::NewStringType::kNormal ).ToLocalChecked() );
	monitorTemplate->InstanceTemplate()->SetInternalFieldCount( 1 ); // 1 internal field for wrap


	constructor.Reset( isolate, monitorTemplate->GetFunction() );
	NODE_SET_PROTOTYPE_METHOD( monitorTemplate, "addFilter", addMonitorFilter );
	//NODE_SET_PROTOTYPE_METHOD( srgTemplate, "addObserver", addMonitorObserver );
	SET_READONLY( exports, "FileMonitor", monitorTemplate->GetFunction() );

}
