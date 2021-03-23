I-Logix-RPY-Archive version 8.0.9 C 820220
{ IProject 
	- _id = GUID cfd53505-8779-41d1-ad7e-c51fe8c46ba7;
	- _myState = 8192;
	- _properties = { IPropertyContainer 
		- Subjects = { IRPYRawContainer 
			- size = 1;
			- value = 
			{ IPropertySubject 
				- _Name = "C_CG";
				- Metaclasses = { IRPYRawContainer 
					- size = 1;
					- value = 
					{ IPropertyMetaclass 
						- _Name = "uClinuxNIOS2";
						- Properties = { IRPYRawContainer 
							- size = 2;
							- value = 
							{ IProperty 
								- _Name = "CPPCompileDebug";
								- _Value = "-g -D_DEBUG -D UCLINUX  -D __LINUX__";
								- _Type = String;
							}
							{ IProperty 
								- _Name = "CPPCompileRelease";
								- _Value = "-D NDEBUG -D _RELEASE -D UCLINUX  -D __LINUX__ -O3";
								- _Type = String;
							}
						}
					}
				}
			}
		}
	}
	- _name = "CoreFrameworkRhp";
	- _lastID = 3;
	- _UserColors = { IRPYRawContainer 
		- size = 16;
		- value = 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 16777215; 
	}
	- _defaultSubsystem = { ISubsystemHandle 
		- _m2Class = "ISubsystem";
		- _filename = "Default.sbs";
		- _subsystem = "";
		- _class = "";
		- _name = "Default";
		- _id = GUID d24ae97f-6010-4401-bc7a-fe251f39b2d7;
	}
	- _component = { IHandle 
		- _m2Class = "IComponent";
		- _filename = "core.cmp";
		- _subsystem = "";
		- _class = "";
		- _name = "core";
		- _id = GUID 3eca3768-cd6a-4edf-b0ab-822f02d35b8a;
	}
	- Multiplicities = { IRPYRawContainer 
		- size = 4;
		- value = 
		{ IMultiplicityItem 
			- _name = "1";
			- _count = 5;
		}
		{ IMultiplicityItem 
			- _name = "*";
			- _count = -1;
		}
		{ IMultiplicityItem 
			- _name = "0,1";
			- _count = -1;
		}
		{ IMultiplicityItem 
			- _name = "1..*";
			- _count = -1;
		}
	}
	- Subsystems = { IRPYRawContainer 
		- size = 4;
		- value = 
		{ ISubsystem 
			- fileName = "Default";
			- _id = GUID d24ae97f-6010-4401-bc7a-fe251f39b2d7;
		}
		{ ISubsystem 
			- fileName = "CoreFramework";
			- _id = GUID e7b1eaf5-d5cb-48d9-9453-7a598d92ab58;
		}
		{ ISubsystem 
			- fileName = "Tests";
			- _id = GUID 4e629766-127b-44db-b49f-17cc2c091292;
		}
		{ IProfile 
			- fileName = "CGCompatibilityPre71C";
			- _id = GUID b17446da-70a7-41cd-bdce-70566f418f7d;
		}
	}
	- Diagrams = { IRPYRawContainer 
		- size = 1;
		- value = 
		{ IDiagram 
			- fileName = "Model1";
			- _id = GUID ae10699c-0875-439d-a9a6-295e591fdcf8;
		}
	}
	- Components = { IRPYRawContainer 
		- size = 12;
		- value = 
		{ IComponent 
			- fileName = "CoreDataTest";
			- _id = GUID df6a9226-16ff-40b8-bcd2-25a6487d76ed;
		}
		{ IComponent 
			- fileName = "CoreArrayTest";
			- _id = GUID fc040aa0-7e1a-4a93-83f7-3ee79a81b9ae;
		}
		{ IComponent 
			- fileName = "CoreDictionaryTest";
			- _id = GUID c9d8a733-d410-4c6c-8424-be1e347f5961;
		}
		{ IComponent 
			- fileName = "CoreRuntimeTest";
			- _id = GUID a788d47d-44a9-458b-aaae-1eed49c54cb5;
		}
		{ IComponent 
			- fileName = "core";
			- _id = GUID 3eca3768-cd6a-4edf-b0ab-822f02d35b8a;
		}
		{ IComponent 
			- fileName = "coreFramework";
			- _id = GUID e6c076fa-7294-4690-b3eb-b05fe9785e18;
		}
		{ IComponent 
			- fileName = "CoreRunLoopTest";
			- _id = GUID 39b795e6-e992-4481-965d-a4126f92741a;
		}
		{ IComponent 
			- fileName = "CoreRunLoopTest2";
			- _id = GUID 6ef8e063-17ca-4596-856a-c9a7fc0c6ba3;
		}
		{ IComponent 
			- fileName = "RiCEventsTest";
			- _id = GUID 10d6a37b-9e60-478b-9cbb-4d265e99c48a;
		}
		{ IComponent 
			- fileName = "CoreMessagePortTest";
			- _id = GUID cc143b77-4439-462b-9607-bf881196e0ae;
		}
		{ IComponent 
			- fileName = "NotificationsTest";
			- _id = GUID 8aae2ce4-3e17-4506-99f1-26994241e3b3;
		}
		{ IComponent 
			- fileName = "CoreTimerTest";
			- _id = GUID 6497ff41-ec3c-4179-bb3e-be48f24d537a;
		}
	}
}

