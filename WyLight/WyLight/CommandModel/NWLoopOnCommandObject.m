//
//  NWLoopOnCommandObject.m
//  WyLightRemote
//
//  Created by Nils Weiß on 13.08.13.
//  Copyright (c) 2013 Nils Weiß. All rights reserved.
//

#import "NWLoopOnCommandObject.h"
#import "WCWiflyControlWrapper.h"

@implementation NWLoopOnCommandObject

- (void)sendToWCWiflyControl:(WCWiflyControlWrapper *)control
{
	[control loopOn];
}

@end