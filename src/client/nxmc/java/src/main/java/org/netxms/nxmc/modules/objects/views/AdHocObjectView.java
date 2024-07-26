/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2023 Raden Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.nxmc.modules.objects.views;

import org.eclipse.jface.resource.ImageDescriptor;
import org.netxms.client.objects.AbstractObject;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.views.View;

/**
 * Ad-hoc views that intended to be shown only for specific object. Ad-hoc views are not dependent on context and always show data
 * for object set during construction.
 */
public abstract class AdHocObjectView extends ObjectView
{
   private long objectId;
   private long contextId;

   /**
    * Create ad-hoc object view.
    *
    * @param name view name
    * @param image view image
    * @param baseId base view ID (actual ID will be derived from base and object ID)
    * @param objectId object ID this view is intended for
    * @param hasFileter true if view should contain filter
    */
   public AdHocObjectView(String name, ImageDescriptor image, String baseId, long objectId, long contextId, boolean hasFilter)
   {
      super(name, image, baseId + "#" + Long.toString(objectId), hasFilter);
      this.objectId = objectId;
      this.contextId = contextId;
   }

   /**
    * @see org.netxms.nxmc.base.views.View#cloneView()
    */
   @Override
   public View cloneView()
   {
      AdHocObjectView view = (AdHocObjectView)super.cloneView();
      view.objectId = objectId;
      view.contextId = contextId;
      return view;
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#isValidForContext(java.lang.Object)
    */
   @Override
   public boolean isValidForContext(Object context)
   {
      return (context != null) && (context instanceof AbstractObject) && 
            ((((AbstractObject)context).getObjectId() == objectId) || (((AbstractObject)context).getObjectId() == contextId));
   }

   /**
    * @see org.netxms.nxmc.base.views.View#isCloseable()
    */
   @Override
   public boolean isCloseable()
   {
      return true;
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#getObject()
    */
   @Override
   public AbstractObject getObject()
   {
      AbstractObject object = super.getObject();
      return ((object != null) && (object.getObjectId() == objectId)) ? object : Registry.getSession().findObjectById(objectId);
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#getObjectId()
    */
   @Override
   public long getObjectId()
   {
      return objectId;
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#getObjectName()
    */
   @Override
   public String getObjectName()
   {
      AbstractObject object = getObject();
      return (object != null) ? object.getObjectName() : "";
   }

   /**
    * @return the contextId
    */
   public long getContextId()
   {
      return contextId;
   }  
}
